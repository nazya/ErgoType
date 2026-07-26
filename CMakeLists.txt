cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ErgoTypeNRF C)

target_compile_definitions(app PRIVATE ERGOTYPE_ZEPHYR=1)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/coreJSON/source/include
    ${CMAKE_CURRENT_LIST_DIR}/pointing
    ${CMAKE_CURRENT_LIST_DIR}/pointing/pmw3360
    ${CMAKE_CURRENT_LIST_DIR}/pointing/pmw3389
)

target_compile_options(app PRIVATE
    -iquote${CMAKE_CURRENT_LIST_DIR}/keyd/port
    -iquote${CMAKE_CURRENT_LIST_DIR}/keyd/src
)

set_source_files_properties(keyd/port/config.c PROPERTIES COMPILE_OPTIONS -Wno-comment)

target_sources(app PRIVATE
    src/main.c
    src/keyscan_zephyr.c
    src/log_zephyr.c
    src/pointing_zephyr.c
    src/ui_zephyr.c
    display/ssd1306.c
    coreJSON/source/core_json.c
    keyd/src/keys.c
    keyd/src/string.c
    keyd/src/unicode.c
    keyd/port/config.c
    keyd/port/daemon.c
    keyd/port/evloop.c
    keyd/port/hooks.c
    keyd/port/keyboard.c
    keyd/port/macro.c
    keyd/port/task.c
    keyd/port/vkbd/tusb_hid.c
    keyd/port/vkbd/vkbd.c
    jconfig.c
    pointing/accel/filter.c
    pointing/accel/filter-adaptive.c
    pointing/accel/filter-adaptive-high-dpi.c
    pointing/accel/filter-adaptive-low-dpi.c
    pointing/accel/filter-custom.c
    pointing/accel/filter-flat.c
    pointing/pmw3360/pmw3360.c
    pointing/pmw3360/srom.c
    pointing/pmw3389/pmw3389.c
    pointing/pmw3389/srom.c
    platform/zephyr/ble.c
    platform/zephyr/ble_config.c
    platform/zephyr/ble_log.c
    platform/zephyr/events.c
    platform/zephyr/gpio.c
    platform/zephyr/hid.c
    platform/zephyr/i2c.c
    platform/zephyr/os.c
    platform/zephyr/spi.c
    platform/zephyr/storage.c
    platform/zephyr/time.c
    platform/zephyr/vkbd_events.c
    platform/zephyr/ws2812.c
)
