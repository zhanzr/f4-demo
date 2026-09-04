# eth_http_server: minimal HTTP server (lwIP NO_SYS raw API + DP83848 PHY +
# RMII + DHCP) on the dev1-f407 board. Based on the STM32F769I-Discovery
# eth_http project, re-targeted to the F407's HAL + the on-board DP83848
# PHY (RMII), serving the dev1-f407 e_server single-page site.
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
set(STM32_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../../board/stm32f407vet6.ld")

# lwIP 2.2.1 (vendored in fire-f429/drivers/lwip - the plain STABLE release,
# not the WICED fork used by the WiFi apps; no WICED platform headers needed).
set(LWIP ${CMAKE_CURRENT_SOURCE_DIR}/../../../fire-f429/drivers/lwip/src)

# lwIP core + netif (NO_SYS raw API - no api/, no sys_arch).
file(GLOB LWIP_CORE ${LWIP}/core/*.c ${LWIP}/core/ipv4/*.c)
file(GLOB LWIP_NETIF ${LWIP}/netif/ethernet.c)

# Regenerate the bundled site from the shared dev1-f407 e_server/ sources
# (e_server/web -> src/web_assets.h).
set(WEB_ASSETS ${CMAKE_CURRENT_SOURCE_DIR}/src/web_assets.h)
add_custom_command(OUTPUT ${WEB_ASSETS}
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/../../e_server/build_web.py
            --out ${WEB_ASSETS}
            --web ${CMAKE_CURRENT_SOURCE_DIR}/../../e_server/web
            --public ${CMAKE_CURRENT_SOURCE_DIR}/../../e_server/public
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../../e_server/build_web.py
            ${CMAKE_CURRENT_SOURCE_DIR}/../../e_server/web/index.html
            ${CMAKE_CURRENT_SOURCE_DIR}/../../e_server/web/style.css
            ${CMAKE_CURRENT_SOURCE_DIR}/../../e_server/web/app.js
    COMMENT "Generating src/web_assets.h from e_server/web")

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/app_ethernet.c
    src/ethernetif.c
    src/dp83848.c
    src/dp83848_phy.c
    src/http_server.c
    src/adc_internal.c
    ${WEB_ASSETS}
    # lwIP (NO_SYS)
    ${LWIP_CORE}
    ${LWIP_NETIF}
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f407_board.cmake)
stm32f407_apply_board(${PROJECT_NAME}.elf "-O2")

target_include_directories(${PROJECT_NAME}.elf PRIVATE
    src
    src/arch
    ${LWIP}/include
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)