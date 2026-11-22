/*
 * DC load Rev A firmware (STM32F103RET6 control board).
 *
 * Safety model (command set and limits: firmware/README.md):
 *   - DAC forced to 0 immediately after the first I2C init, before the PLL is started.
 *   - DISARMED by default; ARM is required for any non-zero DAC code.
 *   - Compile-time cap MAX_DAC_CODE plus a runtime TIER cap that can only be <= MAX_DAC_CODE.
 *   - Heartbeat: while ARMED, no valid command for HEARTBEAT_MS -> DAC 0, DISARMED, flag WDT.
 *   - Software OCP on ISEN (3 consecutive 1 ms samples above threshold) -> DAC 0, latch OCP.
 *   - Software OTP on PC0 (disabled by default: no NTC on the MCU in this test).
 *   - IWDG ~500 ms: a hang resets the MCU, and reset writes DAC 0 again.
 *
 * Pins (netlist_RevA_control_board.txt):
 *   PA1 DUT_ISEN (ADC_IN1)  PA2 DUT_VSEN (ADC_IN2, ignored; ribbon conductor 10 is cut)
 *   PC0 ADC_IN10 (P1 pin 1) PC1 ADC_IN11 (P1 pin 3)
 *   PB8 SCL / PB9 SDA = I2C1 remapped   PB6 TX / PB7 RX = USART1 remapped (CH340E on J3)
 *   PD2 MCU LED (active high)           PC8 scope trigger (P1 pin 2), high while STEP is at code b
 */
#include "stm32f1xx_hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define FW_VERSION      "0.2.0"

/* Two builds (CMake option BENCH_BUILD):
 *   SAFE (default): ceilings from the 2026-11-21 bench measurements, enforced here whatever the host sends.
 *   TEST (BENCH_BUILD): the permissive bench-test firmware used for the characterisation (v0.1 behaviour). */
#ifdef BENCH_BUILD
#define FW_VARIANT      "TEST"
#define MAX_DAC_CODE    1467u           /* ~4.0 A requested (2.727 mA/code) */
#define ENFORCE         0
#else
#define FW_VARIANT      "SAFE"
#define MAX_DAC_CODE    751u            /* 1.90 A with the measured calibration (gate ceiling 1.93 A) */
#define ENFORCE         1
#endif
#define I_MAX_MA        1900u
#define V_MAX_MV        30000u
#define P_MAX_MW        35000u
#define OCP_MARGIN_MA   300u
/* Calibration (run 183314): I = 2.516 mA/code + 10.5 mA; ISEN counts = 311.03/A x I - 0.61 */
#define CAL_UA_PER_CODE 2516.0f
#define CAL_OFFSET_MA   10.5f
#define ISEN_CNT_PER_A  311.03f
#define ISEN_ZERO_CNT   (-0.61f)
/* Thermal model (run 202620), K above ambient per W: heatsink 0.124 @ 17.2 s + 0.865 @ 554.6 s, die 1.97 (fast).
 * Budget = state after 30 W for 180 s from cold; re-arm below 60 % of it. */
#define TH_R1           0.124f
#define TH_TAU1         17.2f
#define TH_R2           0.865f
#define TH_TAU2         554.6f
#define TH_RDIE         1.97f
#define TH_REARM        0.60f

#define MCP4725_ADDR    (0x61u << 1)
#define HEARTBEAT_MS    2000u
#define I2C_TIMEOUT_MS  10u
#define ISEN_COUNTS_PER_A 310.227f      /* 0.25 V/A, 3.3 V / 4095 counts */

#define F_WDT 0x01u
#define F_OCP 0x02u
#define F_OTP 0x04u
#define F_I2C 0x08u
#define F_PWR 0x10u     /* measured power above P_MAX */
#define F_THM 0x20u     /* thermal budget used up */

#define CH_ISEN   1u
#define CH_VSEN   2u
#define CH_NTC    10u
#define CH_NTC2   11u
#define CH_TEMP   16u
#define CH_VREF   17u

static const uint16_t TIER_CODE[6] = {0u, 184u, 367u, 734u, 1100u, 1467u};
static const uint16_t TIER_MA[6]   = {0u, 500u, 1000u, 2000u, 3000u, 4000u};

I2C_HandleTypeDef  hi2c1;
UART_HandleTypeDef huart1;

/* UART line receiver (filled in USART1_IRQHandler) */
volatile char     rx_buf[64];
volatile uint8_t  rx_len;
volatile char     cmd_buf[64];
volatile uint8_t  cmd_ready;

static uint8_t  armed;
static uint8_t  flags;
static uint8_t  tier;          /* 0..5 */
static uint16_t dac_code;
static uint16_t ocp_counts;    /* ISEN threshold, raw counts */
static uint16_t otp_counts;    /* 0 = disabled; trip when NTC counts < otp_counts (hot = low) */
static uint16_t avg_n = 64u;
static uint32_t last_cmd_ms;
static uint8_t  ocp_hits;

/* Limits / thermal model state */
static uint32_t vdut_mv;          /* DUT voltage told by the host (VSEN is broken on Rev A); 0 = unknown */
static float    th_x1, th_x2, th_limit, p_w_live, i_ma_live;
static uint8_t  th_lock, ee_fixed;
__attribute__((unused)) static uint8_t pwr_hits;
static uint32_t isen_acc, isen_n;

/* STEP generator */
static uint8_t  step_active;
static uint16_t step_a, step_b, step_half_ms;
static uint32_t step_halves_left, step_t0;
static uint8_t  step_at_b;

void SystemClock_Config(void);
void Error_Handler(void);

/* ------------------------------------------------------------------ UART */
static void uart_puts(const char *s)
{
    while (*s) {
        while (!(USART1->SR & USART_SR_TXE)) { }
        USART1->DR = (uint8_t)*s++;
    }
    while (!(USART1->SR & USART_SR_TC)) { }
}

static void reply(const char *s)
{
    uart_puts(s);
    uart_puts("\n");
}

/* ------------------------------------------------------------------ MCP4725 */
static HAL_StatusTypeDef dac_raw_write(uint16_t code)
{
    uint8_t d[2] = {(uint8_t)((code >> 8) & 0x0Fu), (uint8_t)(code & 0xFFu)};   /* fast mode, PD = 00 */
    return HAL_I2C_Master_Transmit(&hi2c1, MCP4725_ADDR, d, 2, I2C_TIMEOUT_MS);
}

static void fault(uint8_t f);

static int dac_set(uint16_t code)
{
    if (code > MAX_DAC_CODE) code = 0u;      /* belt and braces */
    if (dac_raw_write(code) != HAL_OK) {
        if (dac_raw_write(code) != HAL_OK) {   /* one retry, then fault */
            fault(F_I2C);
            return -1;
        }
    }
    dac_code = code;
    return 0;
}

static void fault(uint8_t f)
{
    flags |= f;
    armed = 0u;
    step_active = 0u;
    GPIOC->BRR = GPIO_PIN_8;
    if (dac_raw_write(0u) != HAL_OK) {
        (void)dac_raw_write(0u);
        flags |= F_I2C;
    }
    dac_code = 0u;
}

static void disarm(void)
{
    step_active = 0u;
    GPIOC->BRR = GPIO_PIN_8;
    armed = 0u;
    (void)dac_set(0u);
}

static HAL_StatusTypeDef dac_read5(uint8_t r[5])
{
    return HAL_I2C_Master_Receive(&hi2c1, MCP4725_ADDR, r, 5, I2C_TIMEOUT_MS);
}

/* Toggle SCL to free a slave that holds SDA low after an MCU reset mid-transfer. */
static void i2c_bus_recover(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    g.Pin = GPIO_PIN_8; g.Mode = GPIO_MODE_OUTPUT_OD; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &g);
    g.Pin = GPIO_PIN_9; g.Mode = GPIO_MODE_INPUT;
    HAL_GPIO_Init(GPIOB, &g);
    GPIOB->BSRR = GPIO_PIN_8;
    for (int i = 0; i < 9 && !(GPIOB->IDR & GPIO_PIN_9); i++) {
        GPIOB->BRR = GPIO_PIN_8;  for (volatile int d = 0; d < 200; d++) { }
        GPIOB->BSRR = GPIO_PIN_8; for (volatile int d = 0; d < 200; d++) { }
    }
}

static void i2c1_init(void)
{
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) Error_Handler();
}

/* ------------------------------------------------------------------ ADC (register level) */
static void adc_init(void)
{
    __HAL_RCC_ADC_CONFIG(RCC_ADCPCLK2_DIV6);         /* 72 / 6 = 12 MHz */
    __HAL_RCC_ADC1_CLK_ENABLE();
    ADC1->CR1 = 0u;
    ADC1->SMPR1 = 0x00FFFFFFu;                       /* ch10..17: 239.5 cycles */
    ADC1->SMPR2 = 0x3FFFFFFFu;                       /* ch0..9:   239.5 cycles */
    ADC1->SQR1 = 0u;
    ADC1->CR2 = ADC_CR2_ADON;
    HAL_Delay(2);
    ADC1->CR2 |= ADC_CR2_RSTCAL; while (ADC1->CR2 & ADC_CR2_RSTCAL) { }
    ADC1->CR2 |= ADC_CR2_CAL;    while (ADC1->CR2 & ADC_CR2_CAL) { }
    ADC1->CR2 |= ADC_CR2_EXTSEL | ADC_CR2_EXTTRIG | ADC_CR2_TSVREFE;   /* SWSTART trigger */
    HAL_Delay(1);
}

static uint16_t adc_read(uint32_t ch)
{
    ADC1->SQR3 = ch;
    ADC1->CR2 |= ADC_CR2_SWSTART;
    while (!(ADC1->SR & ADC_SR_EOC)) { }
    return (uint16_t)ADC1->DR;
}

static uint32_t adc_sum(uint32_t ch, uint32_t n)
{
    uint32_t s = 0u;
    for (uint32_t i = 0; i < n; i++) s += adc_read(ch);
    return s;
}

/* "1234.56" from a sum of n samples, integer maths only */
static char *fmt_avg(char *p, uint32_t sum, uint32_t n)
{
    uint32_t a100 = (sum * 100u + n / 2u) / n;
    return p + sprintf(p, "%lu.%02lu", (unsigned long)(a100 / 100u), (unsigned long)(a100 % 100u));
}

/* ------------------------------------------------------------------ helpers */
static uint16_t tier_cap(void) { return TIER_CODE[tier] < MAX_DAC_CODE ? TIER_CODE[tier] : MAX_DAC_CODE; }

static void set_tier(uint8_t t)
{
    tier = t;
    ocp_counts = (uint16_t)(((float)TIER_MA[t] + 500.0f) * ISEN_COUNTS_PER_A / 1000.0f);
    if (dac_code > tier_cap()) disarm();
}

static void flags_str(char *p)
{
    p[0] = '\0';
    if (flags & F_WDT) strcat(p, "WDT ");
    if (flags & F_OCP) strcat(p, "OCP ");
    if (flags & F_OTP) strcat(p, "OTP ");
    if (flags & F_I2C) strcat(p, "I2C ");
    if (flags & F_PWR) strcat(p, "PWR ");
    if (flags & F_THM) strcat(p, "THERMAL ");
    size_t l = strlen(p);
    if (l) p[l - 1] = '\0'; else strcpy(p, "NONE");
}

static int parse_u(const char *s, long *out)
{
    char *e;
    if (!s || !*s) return -1;
    long v = strtol(s, &e, 10);
    if (*e != '\0' || v < 0) return -1;
    *out = v;
    return 0;
}

/* ------------------------------------------------------------------ limits and thermal model */
__attribute__((unused)) static float ma_for_code(uint16_t code) { return code ? CAL_OFFSET_MA + code * CAL_UA_PER_CODE / 1000.0f : 0.0f; }

static uint16_t code_for_ma(uint32_t ma)
{
    if (ma <= (uint32_t)CAL_OFFSET_MA) return 0u;
    return (uint16_t)(((float)ma - CAL_OFFSET_MA) * 1000.0f / CAL_UA_PER_CODE + 0.5f);
}

static float isen_ma(float counts)
{
    float i = (counts - ISEN_ZERO_CNT) / ISEN_CNT_PER_A * 1000.0f;
    return i < 0.0f ? 0.0f : i;
}

static void th_step(float *x1, float *x2, float p, float dt)
{
    *x1 += dt / TH_TAU1 * (p - *x1);
    *x2 += dt / TH_TAU2 * (p - *x2);
}

static float th_stress(float x1, float x2, float p) { return TH_R1 * x1 + TH_R2 * x2 + TH_RDIE * p; }

static void th_init(uint8_t assume_hot)
{
    float x1 = 0.0f, x2 = 0.0f;
    for (int i = 0; i < 1800; i++) th_step(&x1, &x2, 30.0f, 0.1f);     /* 30 W for 180 s */
    th_limit = th_stress(x1, x2, 30.0f);
    if (assume_hot) {                     /* state unknown after a non-power-on reset: start at the budget */
        th_x1 = th_x2 = th_limit / (TH_R1 + TH_R2);
        th_lock = 1u;
    } else {
        th_x1 = th_x2 = 0.0f;
        th_lock = 0u;
    }
}

static float th_frac(void) { return th_stress(th_x1, th_x2, p_w_live) / th_limit; }

/* Would running code `code` at the told DUT voltage break the power ceiling? */
static int power_ok(uint16_t code)
{
#if ENFORCE
    return (ma_for_code(code) * (float)vdut_mv / 1000.0f) <= (float)P_MAX_MW;
#else
    (void)code; return 1;
#endif
}

static int arm_allowed(char *why)
{
    if (flags) { strcpy(why, "ERR FAULT_LATCHED"); return 0; }
#if ENFORCE
    if (vdut_mv == 0u) { strcpy(why, "ERR VDUT_NOT_SET"); return 0; }
    if (th_lock) { strcpy(why, "ERR THERMAL_LOCK"); return 0; }
#endif
    return 1;
}

static void apply_ocp_for(uint32_t ma)
{
    uint32_t c = (uint32_t)(((float)(ma + OCP_MARGIN_MA)) * ISEN_CNT_PER_A / 1000.0f);
    ocp_counts = (uint16_t)(c > 4095u ? 4095u : c);
}

/* 10 Hz: live current and power, thermal model, power and thermal trips */
static void limits_tick(float dt)
{
    float counts = isen_n ? (float)isen_acc / (float)isen_n : 0.0f;
    isen_acc = 0u; isen_n = 0u;
    i_ma_live = isen_ma(counts);
    p_w_live = armed ? i_ma_live * (float)vdut_mv / 1.0e6f : 0.0f;
    th_step(&th_x1, &th_x2, p_w_live, dt);
#if ENFORCE
    float f = th_frac();
    if (armed && f >= 1.0f) { th_lock = 1u; fault(F_THM); }
    if (th_lock && !armed && f < TH_REARM) th_lock = 0u;
    if (armed && p_w_live * 1000.0f > (float)P_MAX_MW * 1.05f) {
        if (++pwr_hits >= 3u) fault(F_PWR);
    } else {
        pwr_hits = 0u;
    }
#endif
}

/* Power-up: if the MCP4725 EEPROM is not 0 (finding C5), rewrite it so the load powers up at 0 A. */
static void eeprom_check(void)
{
    uint8_t r[5] = {0};
    if (dac_read5(r) != HAL_OK) return;
    if (((r[3] & 0x0Fu) << 8 | r[4]) == 0u && (r[3] & 0x60u) == 0u) return;
    uint8_t w[3] = {0x60u, 0x00u, 0x00u};
    if (HAL_I2C_Master_Transmit(&hi2c1, MCP4725_ADDR, w, 3, I2C_TIMEOUT_MS) == HAL_OK) {
        HAL_Delay(60);                     /* EEPROM write time (25-50 ms typ) */
        ee_fixed = 1u;
    }
}

/* ------------------------------------------------------------------ commands */
static void handle(char *line)
{
    char out[160];
    char *argv[6] = {0};
    int argc = 0;

    for (char *c = line; *c; c++) *c = (char)toupper((unsigned char)*c);
    for (char *tok = strtok(line, " \t"); tok && argc < 6; tok = strtok(NULL, " \t")) argv[argc++] = tok;
    if (argc == 0) return;
    const char *cmd = argv[0];
    long a = 0, b = 0, c = 0, d = 0;

    last_cmd_ms = HAL_GetTick();     /* any parsed line counts as a heartbeat */

    if (!strcmp(cmd, "*IDN?")) {
        snprintf(out, sizeof out, "ELOAD-REVA-%s,%s,%s %s", FW_VARIANT, FW_VERSION, __DATE__, __TIME__);
        reply(out);
    } else if (!strcmp(cmd, "PING")) {
        reply("PONG");
    } else if (!strcmp(cmd, "ARM")) {
        char why[24];
        if (!arm_allowed(why)) { reply(why); return; }
        armed = 1u; reply("OK");
    } else if (!strcmp(cmd, "VDUT")) {                 /* DUT voltage in mV, told by the host */
        if (argc != 2 || parse_u(argv[1], &a) || a > (long)V_MAX_MV) { reply("ERR ARG"); return; }
        uint32_t old = vdut_mv;
        vdut_mv = (uint32_t)a;
        if (armed && !power_ok(dac_code)) { vdut_mv = old; reply("ERR POWER"); return; }
#if ENFORCE
        if (armed && vdut_mv == 0u) { disarm(); }
#endif
        reply("OK");
    } else if (!strcmp(cmd, "VDUT?")) {
        snprintf(out, sizeof out, "%lu", (unsigned long)vdut_mv); reply(out);
    } else if (!strcmp(cmd, "ISET")) {                 /* current in mA: tier, OCP and code set here */
        if (argc != 2 || parse_u(argv[1], &a)) { reply("ERR ARG"); return; }
        if (a > (long)I_MAX_MA) { reply("ERR CAP"); return; }
        if (!armed && a != 0) { reply("ERR DISARMED"); return; }
        uint16_t c = code_for_ma((uint32_t)a);
        if (c > MAX_DAC_CODE) { reply("ERR CAP"); return; }
        if (!power_ok(c)) { reply("ERR POWER"); return; }
        uint8_t t = 1u;
        while (t < 5u && TIER_CODE[t] < c) t++;
        if (t > tier) { tier = t; }
        if (TIER_CODE[tier] < c) { reply("ERR CAP"); return; }
        apply_ocp_for((uint32_t)a);
        step_active = 0u; GPIOC->BRR = GPIO_PIN_8;
        if (dac_set(c) != 0) { reply("ERR I2C"); return; }
        snprintf(out, sizeof out, "OK %u", c); reply(out);
    } else if (!strcmp(cmd, "LIMITS?")) {
        snprintf(out, sizeof out, "VARIANT=%s,I_MAX_MA=%u,DAC_MAX=%u,V_MAX_MV=%u,P_MAX_MW=%u,OCP_MARGIN_MA=%u,"
                 "THERM=30W_180S,HEARTBEAT_MS=%u", FW_VARIANT, I_MAX_MA, MAX_DAC_CODE, V_MAX_MV, P_MAX_MW,
                 OCP_MARGIN_MA, HEARTBEAT_MS);
        reply(out);
    } else if (!strcmp(cmd, "THERM?")) {               /* budget %, lockout, power mW, current mA */
        snprintf(out, sizeof out, "%u,%u,%lu,%lu", (unsigned)(th_frac() * 100.0f + 0.5f), th_lock,
                 (unsigned long)(p_w_live * 1000.0f), (unsigned long)i_ma_live);
        reply(out);
    } else if (!strcmp(cmd, "THERM:TEST")) {           /* simulate <mW> for <s> from the present state */
        if (argc != 3 || parse_u(argv[1], &a) || parse_u(argv[2], &b) || b > 7200) { reply("ERR ARG"); return; }
        float x1 = th_x1, x2 = th_x2, pw = (float)a / 1000.0f;
        long t_cut = -1;
        for (long s = 1; s <= b; s++) {
            th_step(&x1, &x2, pw, 1.0f);
            if (t_cut < 0 && th_stress(x1, x2, pw) >= th_limit) t_cut = s;
        }
        snprintf(out, sizeof out, "%u,%ld", (unsigned)(th_stress(x1, x2, pw) / th_limit * 100.0f + 0.5f), t_cut);
        reply(out);
    } else if (!strcmp(cmd, "THERM:COLD")) {           /* operator asserts the heatsink is at ambient */
        if (armed) { reply("ERR ARMED"); return; }
        th_x1 = th_x2 = 0.0f; th_lock = 0u; reply("OK");
    } else if (!strcmp(cmd, "DISARM")) {
        disarm(); reply("OK");
    } else if (!strcmp(cmd, "STATE?")) {
        char f[40]; flags_str(f);
        snprintf(out, sizeof out, "%s,%s%s%s", armed ? "ARMED" : "DISARMED", f, th_lock ? ",THERMAL_LOCK" : "",
                 ee_fixed ? ",EEPROM_ZEROED_AT_BOOT" : "");
        reply(out);
    } else if (!strcmp(cmd, "CLEAR")) {
        flags = 0u; ocp_hits = 0u; reply("OK");
    } else if (!strcmp(cmd, "TIER")) {
        if (argc != 2 || parse_u(argv[1], &a) || a > 5) { reply("ERR ARG"); return; }
        set_tier((uint8_t)a); reply("OK");
    } else if (!strcmp(cmd, "TIER?")) {
        snprintf(out, sizeof out, "%u,%u,%u", tier, tier_cap(), ocp_counts); reply(out);
    } else if (!strcmp(cmd, "DAC")) {
        if (argc != 2 || parse_u(argv[1], &a)) { reply("ERR ARG"); return; }
        if (!armed && a != 0) { reply("ERR DISARMED"); return; }
        if (a > tier_cap()) { reply("ERR CAP"); return; }
        if (!power_ok((uint16_t)a)) { reply("ERR POWER"); return; }
        step_active = 0u; GPIOC->BRR = GPIO_PIN_8;
        reply(dac_set((uint16_t)a) == 0 ? "OK" : "ERR I2C");
    } else if (!strcmp(cmd, "DAC?")) {
        snprintf(out, sizeof out, "%u", dac_code); reply(out);
    } else if (!strcmp(cmd, "AVG")) {
        if (argc != 2 || parse_u(argv[1], &a) || a < 1 || a > 1024) { reply("ERR ARG"); return; }
        avg_n = (uint16_t)a; reply("OK");
    } else if (!strcmp(cmd, "OCP")) {
        if (argc != 2 || parse_u(argv[1], &a) || a < 1 || a > 4095) { reply("ERR ARG"); return; }
        ocp_counts = (uint16_t)a; reply("OK");
    } else if (!strcmp(cmd, "OTP")) {
        if (argc != 2 || parse_u(argv[1], &a) || a > 4095) { reply("ERR ARG"); return; }
        otp_counts = (uint16_t)a; reply("OK");
    } else if (!strcmp(cmd, "MEAS?")) {
        uint32_t n = avg_n;
        char *p = out;
        p = fmt_avg(p, adc_sum(CH_ISEN, n), n); *p++ = ',';
        p = fmt_avg(p, adc_sum(CH_VSEN, n), n); *p++ = ',';
        p = fmt_avg(p, adc_sum(CH_NTC, n), n);  *p++ = ',';
        p = fmt_avg(p, adc_sum(CH_NTC2, n), n); *p++ = ',';
        p = fmt_avg(p, adc_sum(CH_VREF, n), n); *p++ = ',';
        p = fmt_avg(p, adc_sum(CH_TEMP, n), n);
        sprintf(p, ",%u,%c%02X,%lu,%lu,%u", dac_code, armed ? 'A' : 'D', flags, (unsigned long)i_ma_live,
                (unsigned long)(p_w_live * 1000.0f), (unsigned)(th_frac() * 100.0f + 0.5f));
        reply(out);
    } else if (!strcmp(cmd, "STEP")) {
        if (argc != 5 || parse_u(argv[1], &a) || parse_u(argv[2], &b) || parse_u(argv[3], &c) || parse_u(argv[4], &d)) {
            reply("ERR ARG"); return;
        }
        if (!armed) { reply("ERR DISARMED"); return; }
        if (a > tier_cap() || b > tier_cap()) { reply("ERR CAP"); return; }
        if (!power_ok((uint16_t)a) || !power_ok((uint16_t)b)) { reply("ERR POWER"); return; }
        if (c < 1 || c > 10000 || d < 1 || d > 1000) { reply("ERR ARG"); return; }
        step_a = (uint16_t)a; step_b = (uint16_t)b; step_half_ms = (uint16_t)c;
        step_halves_left = (uint32_t)d * 2u;
        if (dac_set(step_a) != 0) { reply("ERR I2C"); return; }
        GPIOC->BRR = GPIO_PIN_8; step_at_b = 0u;
        step_t0 = HAL_GetTick(); step_active = 1u;
        reply("OK");
    } else if (!strcmp(cmd, "STEP?")) {
        snprintf(out, sizeof out, "%u,%lu", step_active, (unsigned long)step_halves_left); reply(out);
    } else if (!strcmp(cmd, "EEPROM:ZERO")) {
        if (armed) { reply("ERR ARMED"); return; }
        uint8_t w[3] = {0x60u, 0x00u, 0x00u};       /* write DAC reg + EEPROM, PD = 00, code 0 */
        uint8_t r[5] = {0};
        if (HAL_I2C_Master_Transmit(&hi2c1, MCP4725_ADDR, w, 3, I2C_TIMEOUT_MS) != HAL_OK) { reply("ERR I2C"); return; }
        uint32_t t0 = HAL_GetTick();
        do {
            IWDG->KR = 0xAAAAu;
            if (dac_read5(r) != HAL_OK) { reply("ERR I2C"); return; }
        } while (!(r[0] & 0x80u) && (HAL_GetTick() - t0) < 200u);
        if (!(r[0] & 0x80u)) { reply("ERR RDY_TIMEOUT"); return; }
        uint16_t dac = (uint16_t)((r[1] << 4) | (r[2] >> 4));
        uint16_t ee  = (uint16_t)(((r[3] & 0x0Fu) << 8) | r[4]);
        dac_code = dac;
        snprintf(out, sizeof out, "%s,dac=%u,ee=%u,raw=%02X %02X %02X %02X %02X",
                 (dac == 0u && ee == 0u && (r[3] & 0x60u) == 0u) ? "OK" : "ERR VERIFY",
                 dac, ee, r[0], r[1], r[2], r[3], r[4]);
        reply(out);
    } else if (!strcmp(cmd, "I2C?")) {
        uint8_t r[5] = {0};
        if (HAL_I2C_IsDeviceReady(&hi2c1, MCP4725_ADDR, 2, I2C_TIMEOUT_MS) != HAL_OK) { reply("NACK"); return; }
        if (dac_read5(r) != HAL_OK) { reply("ERR READ"); return; }
        snprintf(out, sizeof out, "ACK,dac=%u,pd=%u,ee=%u,eepd=%u,rdy=%u,por=%u,raw=%02X %02X %02X %02X %02X",
                 (unsigned)((r[1] << 4) | (r[2] >> 4)), (unsigned)((r[0] >> 1) & 3u),
                 (unsigned)(((r[3] & 0x0Fu) << 8) | r[4]), (unsigned)((r[3] >> 5) & 3u),
                 (unsigned)(r[0] >> 7), (unsigned)((r[0] >> 6) & 1u), r[0], r[1], r[2], r[3], r[4]);
        reply(out);
    } else {
        reply("ERR UNKNOWN");
    }
}

/* ------------------------------------------------------------------ GPIO / peripherals */
static void gpio_init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_AFIO_REMAP_SWJ_NOJTAG();             /* PB3 (keypad COL4) free, SWD kept */

    GPIOD->BRR = GPIO_PIN_2;
    GPIOC->BRR = GPIO_PIN_8;
    g.Mode = GPIO_MODE_OUTPUT_PP; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Pin = GPIO_PIN_2; HAL_GPIO_Init(GPIOD, &g);           /* LED */
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Pin = GPIO_PIN_8; HAL_GPIO_Init(GPIOC, &g);           /* scope trigger */

    g.Mode = GPIO_MODE_ANALOG; g.Pull = GPIO_NOPULL;
    g.Pin = GPIO_PIN_1 | GPIO_PIN_2;  HAL_GPIO_Init(GPIOA, &g);
    g.Pin = GPIO_PIN_0 | GPIO_PIN_1;  HAL_GPIO_Init(GPIOC, &g);
    /* Keypad PA4-7 / PB0-3 and encoder PA8-10 stay as reset-default floating inputs. */
}

void HAL_I2C_MspInit(I2C_HandleTypeDef *h)
{
    if (h->Instance != I2C1) return;
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();
    g.Pin = GPIO_PIN_8 | GPIO_PIN_9; g.Mode = GPIO_MODE_AF_OD; g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &g);
    __HAL_AFIO_REMAP_I2C1_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();
}

void HAL_UART_MspInit(UART_HandleTypeDef *h)
{
    if (h->Instance != USART1) return;
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    g.Pin = GPIO_PIN_6; g.Mode = GPIO_MODE_AF_PP; g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &g);
    g.Pin = GPIO_PIN_7; g.Mode = GPIO_MODE_INPUT; g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &g);
    __HAL_AFIO_REMAP_USART1_ENABLE();
}

static void uart1_init(void)
{
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK) Error_Handler();
    USART1->CR1 |= USART_CR1_RXNEIE;
    HAL_NVIC_SetPriority(USART1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

static void iwdg_start(void)
{
    DBGMCU->CR |= DBGMCU_CR_DBG_IWDG_STOP;
    IWDG->KR = 0xCCCCu;      /* start (LSI on) */
    IWDG->KR = 0x5555u;      /* unlock */
    IWDG->PR = 3u;           /* /32 -> ~1.25 kHz from 40 kHz LSI */
    IWDG->RLR = 625u;        /* ~500 ms */
    while (IWDG->SR) { }
    IWDG->KR = 0xAAAAu;
}

/* ------------------------------------------------------------------ main */
int main(void)
{
    HAL_Init();                          /* still on HSI 8 MHz */
    gpio_init();
    i2c_bus_recover();
    i2c1_init();
    (void)dac_raw_write(0u);             /* DAC = 0 before anything else */
    (void)dac_raw_write(0u);

    SystemClock_Config();                /* HSE 8 MHz -> 72 MHz */
    HAL_I2C_DeInit(&hi2c1);
    i2c1_init();                         /* re-time I2C for PCLK1 = 36 MHz */
    if (dac_raw_write(0u) != HAL_OK) flags |= F_I2C;
    dac_code = 0u;

    iwdg_start();
    uart1_init();
    adc_init();
    set_tier(0u);

    if (RCC->CSR & RCC_CSR_IWDGRSTF) flags |= F_WDT;   /* report a watchdog reset */
    uint8_t power_on = (RCC->CSR & RCC_CSR_PORRSTF) ? 1u : 0u;
    __HAL_RCC_CLEAR_RESET_FLAGS();
    /* Thermal state is lost on reset: assume cold only after a power-on reset (USB plugged in). After any other
     * reset (watchdog, button, debugger) assume hot and lock until the model cools (THERM:COLD overrides). */
    th_init(ENFORCE && !power_on);
    eeprom_check();

    uint32_t last_ms = HAL_GetTick(), last_otp = 0u, last_led = 0u, last_lim = HAL_GetTick();
    char line[64];

    for (;;) {
        IWDG->KR = 0xAAAAu;
        uint32_t now = HAL_GetTick();

        if (cmd_ready) {
            __disable_irq();
            memcpy(line, (const void *)cmd_buf, sizeof line);
            cmd_ready = 0u;
            __enable_irq();
            line[sizeof line - 1] = '\0';
            handle(line);
            now = HAL_GetTick();
        }

        if (now != last_ms) {                         /* 1 kHz protection loop */
            last_ms = now;
            uint32_t isen4 = adc_sum(CH_ISEN, 4u);
            uint32_t isen = isen4 / 4u;
            isen_acc += isen4; isen_n += 4u;
            if (dac_code > 0u && isen > ocp_counts) {
                if (++ocp_hits >= 3u) fault(F_OCP);
            } else {
                ocp_hits = 0u;
            }
            if (otp_counts && (now - last_otp) >= 50u) {
                last_otp = now;
                if (adc_sum(CH_NTC, 8u) / 8u < otp_counts) fault(F_OTP);
            }
        }

        if (armed && (now - last_cmd_ms) > HEARTBEAT_MS) fault(F_WDT);

        if ((now - last_lim) >= 100u) {                /* 10 Hz: power, thermal model, trips */
            limits_tick((float)(now - last_lim) / 1000.0f);
            last_lim = now;
        }

        if (step_active && (now - step_t0) >= step_half_ms) {
            step_t0 += step_half_ms;
            step_at_b ^= 1u;
            if (dac_set(step_at_b ? step_b : step_a) == 0) {
                if (step_at_b) GPIOC->BSRR = GPIO_PIN_8; else GPIOC->BRR = GPIO_PIN_8;
            }
            if (--step_halves_left == 0u) { step_active = 0u; GPIOC->BRR = GPIO_PIN_8; }
        }

        uint32_t period = flags ? 100u : (armed ? 125u : 500u);   /* fault: fast, armed: 4 Hz, idle: 1 Hz */
        if ((now - last_led) >= period) { last_led = now; GPIOD->ODR ^= GPIO_PIN_2; }
    }
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef o = {0};
    RCC_ClkInitTypeDef c = {0};
    o.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    o.HSEState = RCC_HSE_ON;
    o.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    o.HSIState = RCC_HSI_ON;
    o.PLL.PLLState = RCC_PLL_ON;
    o.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    o.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&o) != HAL_OK) Error_Handler();
    c.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    c.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    c.AHBCLKDivider = RCC_SYSCLK_DIV1;
    c.APB1CLKDivider = RCC_HCLK_DIV2;
    c.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&c, FLASH_LATENCY_2) != HAL_OK) Error_Handler();
}

void Error_Handler(void)
{
    /* DAC was zeroed before the clock switch. Blink fast forever; the IWDG (if running) resets us. */
    __disable_irq();
    for (;;) {
        GPIOD->ODR ^= GPIO_PIN_2;
        for (volatile uint32_t d = 0; d < 100000u; d++) { }
    }
}

/* ------------------------------------------------------------------ interrupts */
void SysTick_Handler(void) { HAL_IncTick(); }
void NMI_Handler(void) { }
void HardFault_Handler(void) { for (;;) { } }   /* IWDG resets the MCU; reset zeroes the DAC */
void MemManage_Handler(void) { for (;;) { } }
void BusFault_Handler(void) { for (;;) { } }
void UsageFault_Handler(void) { for (;;) { } }
void SVC_Handler(void) { }
void DebugMon_Handler(void) { }
void PendSV_Handler(void) { }

void USART1_IRQHandler(void)
{
    uint32_t sr = USART1->SR;
    if (sr & (USART_SR_RXNE | USART_SR_ORE)) {
        char ch = (char)USART1->DR;                 /* reading DR also clears ORE */
        if (ch == '\r') return;
        if (ch == '\n') {
            if (!cmd_ready) {
                for (uint8_t i = 0; i < rx_len; i++) cmd_buf[i] = rx_buf[i];
                cmd_buf[rx_len] = '\0';
                cmd_ready = 1u;
            }
            rx_len = 0u;
        } else if (rx_len < sizeof rx_buf - 1u) {
            rx_buf[rx_len++] = ch;
        }
    }
}
