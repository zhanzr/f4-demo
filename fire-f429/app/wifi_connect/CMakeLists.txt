# WiFi connect for fire-f429 (EMW1062/AP6181 via SDIO): join an AP + DHCP.
# Based on the wifi_scan project; adds wwd_wifi_join() + LwIP DHCP.
cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
endif()

project(wifi_connect C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(STM32_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../../board/stm32f429_sdram.ld")
set(STM32_SKIP_BOARD_IT ON)
set(WIFI_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/wifi_ap6181")

# LwIP 2.0.3 core + api + netif (not the apps).
file(GLOB LWIP_API  ${WIFI_SDK}/WICED/network/LwIP/ver2.0.3/src/api/*.c)
file(GLOB LWIP_CORE ${WIFI_SDK}/WICED/network/LwIP/ver2.0.3/src/core/*.c
                    ${WIFI_SDK}/WICED/network/LwIP/ver2.0.3/src/core/ipv4/*.c
                    ${WIFI_SDK}/WICED/network/LwIP/ver2.0.3/src/core/ipv6/*.c)
file(GLOB LWIP_NETIF ${WIFI_SDK}/WICED/network/LwIP/ver2.0.3/src/netif/ethernet.c)

set(WWD_PLATFORM ${WIFI_SDK}/WICED/platform/MCU/STM32F4xx)
set(STDPERIPH ${WWD_PLATFORM}/peripherals/libraries)

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/connect.c
    src/it.c
    ${WIFI_SDK}/scan_app/43362A2_bin.c
    # FreeRTOS 9.0.0
    ${WIFI_SDK}/WICED/RTOS/FreeRTOS/ver9.0.0/Source/croutine.c
    ${WIFI_SDK}/WICED/RTOS/FreeRTOS/ver9.0.0/Source/event_groups.c
    ${WIFI_SDK}/WICED/RTOS/FreeRTOS/ver9.0.0/Source/list.c
    ${WIFI_SDK}/WICED/RTOS/FreeRTOS/ver9.0.0/Source/queue.c
    ${WIFI_SDK}/WICED/RTOS/FreeRTOS/ver9.0.0/Source/tasks.c
    ${WIFI_SDK}/WICED/RTOS/FreeRTOS/ver9.0.0/Source/timers.c
    ${WIFI_SDK}/WICED/RTOS/FreeRTOS/ver9.0.0/Source/portable/GCC/ARM_CM4F/port.c
    ${WIFI_SDK}/WICED/RTOS/FreeRTOS/ver9.0.0/Source/portable/MemMang/heap_4.c
    # WWD RTOS glue + low level init
    ${WIFI_SDK}/WICED/RTOS/FreeRTOS/WWD/wwd_rtos.c
    ${WIFI_SDK}/WICED/RTOS/FreeRTOS/WWD/ARM_CM7/low_level_init.c
    # LwIP
    ${LWIP_API}
    ${LWIP_CORE}
    ${LWIP_NETIF}
    ${WIFI_SDK}/WICED/network/LwIP/WWD/FreeRTOS/sys_arch.c
    ${WIFI_SDK}/WICED/network/LwIP/WWD/wwd_buffer.c
    ${WIFI_SDK}/WICED/network/LwIP/WWD/wwd_network.c
    # WWD core
    ${WIFI_SDK}/WICED/WWD/internal/wwd_ap_common.c
    ${WIFI_SDK}/WICED/WWD/internal/wwd_debug.c
    ${WIFI_SDK}/WICED/WWD/internal/wwd_eapol.c
    ${WIFI_SDK}/WICED/WWD/internal/wwd_internal.c
    ${WIFI_SDK}/WICED/WWD/internal/wwd_logging.c
    ${WIFI_SDK}/WICED/WWD/internal/wwd_management.c
    ${WIFI_SDK}/WICED/WWD/internal/wwd_rtos_interface.c
    ${WIFI_SDK}/WICED/WWD/internal/wwd_sdpcm.c
    ${WIFI_SDK}/WICED/WWD/internal/wwd_thread.c
    ${WIFI_SDK}/WICED/WWD/internal/wwd_thread_internal.c
    ${WIFI_SDK}/WICED/WWD/internal/wwd_wifi.c
    ${WIFI_SDK}/WICED/WWD/internal/wwd_wifi_chip_common.c
    ${WIFI_SDK}/WICED/WWD/internal/wwd_wifi_sleep.c
    ${WIFI_SDK}/WICED/WWD/internal/bus_protocols/wwd_bus_common.c
    ${WIFI_SDK}/WICED/WWD/internal/bus_protocols/SDIO/wwd_bus_protocol.c
    ${WIFI_SDK}/WICED/WWD/internal/chips/43362/wwd_ap.c
    ${WIFI_SDK}/WICED/WWD/internal/chips/43362/wwd_chip_specific_functions.c
    # WICED core
    ${WIFI_SDK}/WICED/internal/wiced_lib.c
    ${WIFI_SDK}/WICED/internal/wiced_crypto.c
    # Platform glue
    ${WIFI_SDK}/WICED/platform/MCU/platform_resource.c
    ${WIFI_SDK}/WICED/platform/MCU/wwd_resources.c
    ${WIFI_SDK}/WICED/platform/MCU/wwd_platform_separate_mcu.c
    ${WIFI_SDK}/WICED/platform/MCU/platform_stdio.c
    ${WIFI_SDK}/WICED/platform/ARM_CM4/host_cm4.c
    ${WWD_PLATFORM}/platform_init.c
    ${WWD_PLATFORM}/platform_unhandled_isr.c
    ${WWD_PLATFORM}/platform_vector_table.c
    ${WWD_PLATFORM}/WWD/wwd_platform.c
    ${WWD_PLATFORM}/WWD/wwd_SDIO.c
    ${WWD_PLATFORM}/WWD/platform.c
    ${WWD_PLATFORM}/peripherals/platform_gpio.c
    ${WWD_PLATFORM}/peripherals/platform_mcu_powersave.c
    ${WWD_PLATFORM}/peripherals/platform_rtc.c
    ${WWD_PLATFORM}/peripherals/platform_uart.c
    ${WWD_PLATFORM}/peripherals/platform_watchdog.c
    # Utilities
    ${WIFI_SDK}/libraries/utilities/TLV/tlv.c
    ${WIFI_SDK}/libraries/utilities/ring_buffer/ring_buffer.c
    # StdPeriph (SDIO/DMA/GPIO/... used by the WWD platform glue)
    ${STDPERIPH}/src/misc.c
    ${STDPERIPH}/src/stm32f4xx_gpio.c
    ${STDPERIPH}/src/stm32f4xx_rcc.c
    ${STDPERIPH}/src/stm32f4xx_sdio.c
    ${STDPERIPH}/src/stm32f4xx_dma.c
    ${STDPERIPH}/src/stm32f4xx_usart.c
    ${STDPERIPH}/src/stm32f4xx_pwr.c
    ${STDPERIPH}/src/stm32f4xx_syscfg.c
    ${STDPERIPH}/src/stm32f4xx_exti.c
    ${STDPERIPH}/src/stm32f4xx_tim.c
    ${STDPERIPH}/src/stm32f4xx_adc.c
    ${STDPERIPH}/src/stm32f4xx_spi.c
    ${STDPERIPH}/src/stm32f4xx_i2c.c
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f429_board.cmake)
stm32f429_apply_board(${PROJECT_NAME}.elf "-O2")
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE
    DATA_IN_ExtSDRAM
    STM32F429_439xx
    USE_STDPERIPH_DRIVER
    LWIP_TIMEVAL_PRIVATE=1
    WICED_DISABLE_BOOTLOADER
    WICED_DISABLE_MCU_POWERSAVE
)

target_include_directories(${PROJECT_NAME}.elf PRIVATE
    src
    ${WIFI_SDK}/scan_app
    ${WIFI_SDK}/include
    ${WIFI_SDK}/libraries/utilities/linked_list
    ${WIFI_SDK}/libraries/utilities/TLV
    ${WIFI_SDK}/libraries/utilities/ring_buffer
    ${WIFI_SDK}/libraries/inputs/gpio_button
    ${WIFI_SDK}/resources/firmware/43362
    ${WIFI_SDK}/WICED/internal
    ${WIFI_SDK}/WICED/internal/unit
    ${WIFI_SDK}/WICED/WWD
    ${WIFI_SDK}/WICED/WWD/include
    ${WIFI_SDK}/WICED/WWD/include/network
    ${WIFI_SDK}/WICED/WWD/include/platform
    ${WIFI_SDK}/WICED/WWD/include/RTOS
    ${WIFI_SDK}/WICED/WWD/internal
    ${WIFI_SDK}/WICED/WWD/internal/bus_protocols
    ${WIFI_SDK}/WICED/WWD/internal/bus_protocols/SDIO
    ${WIFI_SDK}/WICED/WWD/internal/chips/43362
    ${WIFI_SDK}/WICED/RTOS/FreeRTOS/ver9.0.0/Source/include
    ${WIFI_SDK}/WICED/RTOS/FreeRTOS/ver9.0.0/Source
    ${WIFI_SDK}/WICED/RTOS/FreeRTOS/ver9.0.0/Source/portable/GCC/ARM_CM4F
    ${WIFI_SDK}/WICED/RTOS/FreeRTOS/WWD
    ${WIFI_SDK}/WICED/RTOS/FreeRTOS/WWD/ARM_CM3
    ${WIFI_SDK}/WICED/RTOS/FreeRTOS/WICED
    ${WIFI_SDK}/WICED/network/LwIP/ver2.0.3/src
    ${WIFI_SDK}/WICED/network/LwIP/ver2.0.3/src/include
    ${WIFI_SDK}/WICED/network/LwIP/WWD
    ${WIFI_SDK}/WICED/network/LwIP/WWD/FreeRTOS
    ${WIFI_SDK}/WICED/network/LwIP/WWD/FreeRTOS/arch
    ${WIFI_SDK}/WICED/network/LwIP/WICED
    ${WIFI_SDK}/WICED/platform/include
    ${WIFI_SDK}/WICED/platform/MCU
    ${WIFI_SDK}/WICED/platform/MCU/STM32F4xx
    ${WIFI_SDK}/WICED/platform/MCU/STM32F4xx/peripherals
    ${WIFI_SDK}/WICED/platform/MCU/STM32F4xx/WWD
    ${WIFI_SDK}/WICED/platform/ARM_CM4
    ${WIFI_SDK}/WICED/platform/GCC
    ${WIFI_SDK}/WICED/security/BESL/include
    ${WIFI_SDK}/WICED/security/BESL/crypto_internal
    ${WIFI_SDK}/WICED/security/BESL/mbedtls_open/include
    ${WIFI_SDK}/WICED/security/BESL/host/WICED
    ${WIFI_SDK}/WICED/security/PostgreSQL/include
    ${STDPERIPH}/inc
)

# The vendored WICED/StdPeriph sources use the StdPeriph headers and the OLD
# CMSIS device header (BSRRL/BSRRH, u8, HSE_STARTUP_TIMEOUT...), which conflict
# with the HAL types / modern CMSIS if USE_HAL_DRIVER is visible. Compile those
# sources with the HAL define removed and the old device header forced first.
set(WIFI_STDPERIPH_CONF ${STDPERIPH}/stm32f4xx_conf.h)
set(WIFI_OLD_DEVICE ${STDPERIPH}/stm32f4xx.h)
get_target_property(WIFI_SOURCES ${PROJECT_NAME}.elf SOURCES)
foreach(_src IN LISTS WIFI_SOURCES)
    if(_src MATCHES "wifi_ap6181")
        set_source_files_properties(${_src} PROPERTIES
            COMPILE_OPTIONS
            "-UUSE_HAL_DRIVER;-I;${WIFI_SDK}/CMSIS/Include;-include;${WIFI_OLD_DEVICE};-include;${WIFI_STDPERIPH_CONF}")
    endif()
endforeach()

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)
