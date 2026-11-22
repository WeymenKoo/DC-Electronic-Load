cmake_minimum_required(VERSION 3.20)
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/arm-gcc.cmake)
project(eload_reva C ASM)

option(BENCH_BUILD "permissive bench-test build (v0.1 behaviour, no ceilings)" OFF)

set(HAL Drivers/STM32F1xx_HAL_Driver/Src)
add_executable(eload_reva.elf
    Core/Src/main.c
    Core/Src/system_stm32f1xx.c
    Core/Startup/startup_stm32f103retx.s
    ${HAL}/stm32f1xx_hal.c
    ${HAL}/stm32f1xx_hal_cortex.c
    ${HAL}/stm32f1xx_hal_rcc.c
    ${HAL}/stm32f1xx_hal_rcc_ex.c
    ${HAL}/stm32f1xx_hal_gpio.c
    ${HAL}/stm32f1xx_hal_gpio_ex.c
    ${HAL}/stm32f1xx_hal_i2c.c
    ${HAL}/stm32f1xx_hal_uart.c
    ${HAL}/stm32f1xx_hal_dma.c
)
target_include_directories(eload_reva.elf PRIVATE
    Core/Inc
    Drivers/STM32F1xx_HAL_Driver/Inc
    Drivers/CMSIS/Device/ST/STM32F1xx/Include
    Drivers/CMSIS/Include)
target_compile_definitions(eload_reva.elf PRIVATE USE_HAL_DRIVER STM32F103xE $<$<BOOL:${BENCH_BUILD}>:BENCH_BUILD>)
set(CPU -mcpu=cortex-m3 -mthumb)
target_compile_options(eload_reva.elf PRIVATE ${CPU} -Os -g3 -Wall -ffunction-sections -fdata-sections)
target_link_options(eload_reva.elf PRIVATE ${CPU} -T${CMAKE_CURRENT_SOURCE_DIR}/STM32F103RETX_FLASH.ld
    --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections -Wl,-Map=eload_reva.map -Wl,--print-memory-usage)
add_custom_command(TARGET eload_reva.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex eload_reva.elf eload_reva.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary eload_reva.elf eload_reva.bin
    COMMAND ${CMAKE_SIZE} eload_reva.elf)
