# eth_http_server: minimal HTTP server on the apollo-f429 (LAN8720A PHY + RMII
# + static IP), stage-2 SDRAM/NAND app. Serves the bundled e_server single-page
# site (2 LEDs, internal-ADC + DHT11 sensors, board info).
#
#   bash build.sh           # == cmake -G Ninja .. && ninja
#   ninja flash             # -> NAND via the FMC-NAND algo, then reset
#   ninja flash-boot        # (helper) programs tool/boot into internal flash too

cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
endif()

project(eth_http_server C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Stage-2 app: link entirely in SDRAM at 0xC0000000 and use the non-destructive
# SystemInit (the bootloader owns the clock tree). app.ld puts the ETH DMA
# buffers (.sram_dma) in internal SRAM where the MAC can reach them.
set(STM32_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/app.ld" CACHE FILEPATH
    "stage-2 app linker script (SDRAM at 0xC0000000)" FORCE)
set(STM32F429_SYSTEM_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/system_app.c" CACHE FILEPATH
    "stage-2 app system init (non-destructive)" FORCE)

# Shared board drivers referenced from bare/lcd_touch_test/src (single copy).
set(BARE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../bare/lcd_touch_test/src")

# lwIP 2.2.1 (vendored in apollo-f429/drivers/lwip - the plain STABLE release).
set(LWIP ${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/lwip/src)

# lwIP core + netif (NO_SYS raw API - no api/, no sys_arch).
file(GLOB LWIP_CORE ${LWIP}/core/*.c ${LWIP}/core/ipv4/*.c)
file(GLOB LWIP_NETIF ${LWIP}/netif/ethernet.c)

# Regenerate the bundled site from e_server/ (apollo-f429 copy):
# e_server/web + e_server/public -> src/web_assets.h.
set(WEB_ASSETS ${CMAKE_CURRENT_SOURCE_DIR}/src/web_assets.h)
set(E_SERVER ${CMAKE_CURRENT_SOURCE_DIR}/../../e_server)
add_custom_command(OUTPUT ${WEB_ASSETS}
    COMMAND python ${E_SERVER}/build_web.py
            --out ${WEB_ASSETS}
            --web ${E_SERVER}/web
            --public ${E_SERVER}/public
    DEPENDS ${E_SERVER}/build_web.py
            ${E_SERVER}/web/index.html
            ${E_SERVER}/web/style.css
            ${E_SERVER}/web/app.js
            ${E_SERVER}/public/f429_board_0.jpg
            ${E_SERVER}/public/f429_board_v1.jpg
    COMMENT "Generating src/web_assets.h from e_server/web + e_server/public")

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/app_ethernet.c
    src/ethernetif.c
    src/lan8720a.c
    src/http_server.c
    ${BARE_SRC}/adc_internal.c
    ${BARE_SRC}/dht11.c
    ${BARE_SRC}/pcf8574.c
    ${BARE_SRC}/vendor/eeprom/myiic.c
    ${BARE_SRC}/delay.c
    ${WEB_ASSETS}
    # lwIP (NO_SYS)
    ${LWIP_CORE}
    ${LWIP_NETIF}
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f429_board.cmake)
stm32f429_apply_board(${PROJECT_NAME}.elf "-O2")

target_compile_definitions(${PROJECT_NAME}.elf PRIVATE NAND_APP=1)

target_include_directories(${PROJECT_NAME}.elf PRIVATE
    src
    src/arch
    ${BARE_SRC}
    ${BARE_SRC}/vendor/eeprom
    ${LWIP}/include
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/nand-flash-targets.cmake)