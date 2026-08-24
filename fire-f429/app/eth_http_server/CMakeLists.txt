# eth_http_server: minimal HTTP server (lwIP NO_SYS raw API + LAN8720A PHY +
# RMII + DHCP) on the fire-f429 board. Based on the STM32F769I-Discovery
# eth_http project, re-targeted to the F429's HAL + the on-board LAN8720A
# PHY (RMII), serving the e_server single-page site.
#
#   cmake -G Ninja -B build .
#   ninja -C build        # also regenerates src/web_assets.h from e_server/
#   ninja -C build flash

cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
endif()

project(eth_http_server C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(STM32_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../../board/stm32f429_sdram.ld")

set(WIFI_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/wifi_ap6181")
set(LWIP ${WIFI_SDK}/WICED/network/LwIP/ver2.0.3/src)

# lwIP 2.0.3 core + netif (NO_SYS raw API - no api/, no FreeRTOS sys_arch).
file(GLOB LWIP_CORE ${LWIP}/core/*.c ${LWIP}/core/ipv4/*.c)
file(GLOB LWIP_NETIF ${LWIP}/netif/ethernet.c)

# Regenerate the bundled site from the shared e_server/ sources
# (e_server/web + e_server/public -> src/web_assets.h).
set(WEB_ASSETS ${CMAKE_CURRENT_SOURCE_DIR}/src/web_assets.h)
add_custom_command(OUTPUT ${WEB_ASSETS}
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/../../../e_server/build_web.py
            --out ${WEB_ASSETS}
            --web ${CMAKE_CURRENT_SOURCE_DIR}/../../../e_server/web
            --public ${CMAKE_CURRENT_SOURCE_DIR}/../../../e_server/public
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../../../e_server/build_web.py
            ${CMAKE_CURRENT_SOURCE_DIR}/../../../e_server/web/index.html
            ${CMAKE_CURRENT_SOURCE_DIR}/../../../e_server/web/style.css
            ${CMAKE_CURRENT_SOURCE_DIR}/../../../e_server/web/app.js
            ${CMAKE_CURRENT_SOURCE_DIR}/../../../e_server/public/f429_board_0.jpg
            ${CMAKE_CURRENT_SOURCE_DIR}/../../../e_server/public/f429_board_v1.jpg
    COMMENT "Generating src/web_assets.h from e_server/web + e_server/public")

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/app_ethernet.c
    src/ethernetif.c
    src/lan8720a.c
    src/http_server.c
    ../board_hello/src/adc_internal.c
    ../board_hello/src/dht11.c
    ../board_hello/src/mpu6050.c
    ${WEB_ASSETS}
    # lwIP (NO_SYS)
    ${LWIP_CORE}
    ${LWIP_NETIF}
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f429_board.cmake)
stm32f429_apply_board(${PROJECT_NAME}.elf "-O2")
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE
    DATA_IN_ExtSDRAM
)

target_include_directories(${PROJECT_NAME}.elf PRIVATE
    src
    src/arch
    ${CMAKE_CURRENT_SOURCE_DIR}/../board_hello/src
    ${WIFI_SDK}/WICED/platform/GCC
    ${LWIP}/include
    ${LWIP}/include/netif
    ${LWIP}/include/ipv4
)

add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex ${PROJECT_NAME}.elf ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary ${PROJECT_NAME}.elf ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_NAME}.hex ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.hex
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_NAME}.bin ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} ${PROJECT_NAME}.elf
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "objcopy -> .hex/.bin, size")
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)