if(NOT DEFINED ERGOTYPE_ROOT)
    message(FATAL_ERROR "ERGOTYPE_ROOT must be set before including component.cmake")
endif()

if(NOT DEFINED ERGOTYPE_ESP_TARGET)
    message(FATAL_ERROR "ERGOTYPE_ESP_TARGET must be set before including component.cmake")
endif()

set(ERGOTYPE_SOURCES
    ${ERGOTYPE_ROOT}/main.c
    ${ERGOTYPE_ROOT}/keyscan.c
    ${ERGOTYPE_ROOT}/jconfig.c
    ${ERGOTYPE_ROOT}/log.c
    ${ERGOTYPE_ROOT}/led/led.c
    ${ERGOTYPE_ROOT}/display/ssd1306.c
    ${ERGOTYPE_ROOT}/sensors/vl53l4cd/vl53l4cd.c
    ${ERGOTYPE_ROOT}/ui/ui.c
    ${ERGOTYPE_ROOT}/pointing/accel/filter.c
    ${ERGOTYPE_ROOT}/pointing/accel/filter-adaptive.c
    ${ERGOTYPE_ROOT}/pointing/accel/filter-adaptive-high-dpi.c
    ${ERGOTYPE_ROOT}/pointing/accel/filter-adaptive-low-dpi.c
    ${ERGOTYPE_ROOT}/pointing/accel/filter-custom.c
    ${ERGOTYPE_ROOT}/pointing/accel/filter-flat.c
    ${ERGOTYPE_ROOT}/pointing/pointer.c
    ${ERGOTYPE_ROOT}/pointing/pmw3360/pmw3360.c
    ${ERGOTYPE_ROOT}/pointing/pmw3360/srom.c
    ${ERGOTYPE_ROOT}/pointing/pmw3389/pmw3389.c
    ${ERGOTYPE_ROOT}/pointing/pmw3389/srom.c
    ${ERGOTYPE_ROOT}/keyd/port/config.c
    ${ERGOTYPE_ROOT}/keyd/port/macro.c
    ${ERGOTYPE_ROOT}/keyd/port/keyboard.c
    ${ERGOTYPE_ROOT}/keyd/port/evloop.c
    ${ERGOTYPE_ROOT}/keyd/port/daemon.c
    ${ERGOTYPE_ROOT}/keyd/port/hooks.c
    ${ERGOTYPE_ROOT}/keyd/port/task.c
    ${ERGOTYPE_ROOT}/keyd/port/vkbd/vkbd.c
    ${ERGOTYPE_ROOT}/keyd/port/vkbd/tusb_hid.c
    ${ERGOTYPE_ROOT}/keyd/src/keys.c
    ${ERGOTYPE_ROOT}/keyd/src/string.c
    ${ERGOTYPE_ROOT}/keyd/src/unicode.c
    ${ERGOTYPE_ROOT}/coreJSON/source/core_json.c
    ${ERGOTYPE_ROOT}/tusb/usb_descriptors.c
    ${ERGOTYPE_ROOT}/tusb/usb_cdc_callbacks.c
    ${ERGOTYPE_ROOT}/tusb/usb_device_callbacks.c
    ${ERGOTYPE_ROOT}/tusb/usb_hid_callbacks.c
    ${ERGOTYPE_ROOT}/tusb/usb_webhid.c
    ${ERGOTYPE_ROOT}/tusb/usb_msc_callbacks.c
    ${CMAKE_CURRENT_LIST_DIR}/entry.c
    ${ERGOTYPE_ESP_TARGET}/board.c
    ${CMAKE_CURRENT_LIST_DIR}/os.c
    ${CMAKE_CURRENT_LIST_DIR}/printf.c
    ${ERGOTYPE_ROOT}/storage/fatfs.c
    ${ERGOTYPE_ROOT}/storage/volume.c
    ${ERGOTYPE_ROOT}/storage/fat12_image.c
    ${CMAKE_CURRENT_LIST_DIR}/storage_backend.c
    ${CMAKE_CURRENT_LIST_DIR}/storage_diskio.c
    ${CMAKE_CURRENT_LIST_DIR}/time.c
    ${CMAKE_CURRENT_LIST_DIR}/usb_tinyusb.c
    ${ERGOTYPE_ROOT}/platform/tinyusb/cdc_tinyusb.c
    ${ERGOTYPE_ROOT}/platform/tinyusb/hid_tinyusb.c
    ${CMAKE_CURRENT_LIST_DIR}/io.c
    ${CMAKE_CURRENT_LIST_DIR}/led_strip_rmt.c
)

idf_component_register(
    SRCS ${ERGOTYPE_SOURCES}
    INCLUDE_DIRS
        .
        ${ERGOTYPE_ROOT}
        ${ERGOTYPE_ROOT}/platform/include
        ${ERGOTYPE_ROOT}/tusb
        ${ERGOTYPE_ROOT}/display
        ${ERGOTYPE_ROOT}/keyd/port
        ${ERGOTYPE_ROOT}/keyd/port/vkbd
        ${ERGOTYPE_ROOT}/pointing
        ${ERGOTYPE_ROOT}/pointing/pmw3360
        ${ERGOTYPE_ROOT}/pointing/pmw3389
        ${ERGOTYPE_ROOT}/coreJSON/source/include
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/include
    REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_spi esp_timer fatfs esp_partition esp_hw_support tinyusb led_strip
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=switch
    -Wno-error=format-truncation
    -Wno-error=comment
    -iquote${ERGOTYPE_ROOT}/keyd/port
    -iquote${ERGOTYPE_ROOT}/keyd/src
)

if(DEFINED ERGOTYPE_TINYUSB_RHPORT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BOARD_TUD_RHPORT=${ERGOTYPE_TINYUSB_RHPORT})
endif()

if(DEFINED ERGOTYPE_TINYUSB_MAX_SPEED)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BOARD_TUD_MAX_SPEED=${ERGOTYPE_TINYUSB_MAX_SPEED})
endif()

if(DEFINED ERGOTYPE_USB_COMPOSITE_NKRO_KB_MOUSE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ERGOTYPE_USB_COMPOSITE_NKRO_KB_MOUSE=${ERGOTYPE_USB_COMPOSITE_NKRO_KB_MOUSE})
endif()

if(DEFINED ERGOTYPE_BOARD_DFR1172)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ERGOTYPE_BOARD_DFR1172=${ERGOTYPE_BOARD_DFR1172})
endif()

if(DEFINED ERGOTYPE_BOARD_WAVESHARE_ESP32S3_PICO)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ERGOTYPE_BOARD_WAVESHARE_ESP32S3_PICO=${ERGOTYPE_BOARD_WAVESHARE_ESP32S3_PICO})
endif()

if(DEFINED ERGOTYPE_LED_STRIP_RGB)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ERGOTYPE_LED_STRIP_RGB=${ERGOTYPE_LED_STRIP_RGB})
endif()

set(ERGOTYPE_TINYUSB_LINK_U
    -Wl,-u,tud_descriptor_device_cb
    -Wl,-u,tud_descriptor_configuration_cb
    -Wl,-u,tud_descriptor_string_cb
    -Wl,-u,tud_hid_descriptor_report_cb
    -Wl,-u,tud_hid_report_complete_cb
    -Wl,-u,tud_hid_get_report_cb
    -Wl,-u,tud_hid_set_report_cb
    -Wl,-u,tud_cdc_rx_cb
    -Wl,-u,tud_cdc_line_state_cb
    -Wl,-u,tud_mount_cb
    -Wl,-u,tud_umount_cb
    -Wl,-u,tud_suspend_cb
    -Wl,-u,tud_resume_cb
    -Wl,-u,tud_msc_inquiry_cb
    -Wl,-u,tud_msc_test_unit_ready_cb
    -Wl,-u,tud_msc_capacity_cb
    -Wl,-u,tud_msc_start_stop_cb
    -Wl,-u,tud_msc_read10_cb
    -Wl,-u,tud_msc_is_writable_cb
    -Wl,-u,tud_msc_write10_cb
    -Wl,-u,tud_msc_scsi_cb
)
if(DEFINED ERGOTYPE_TINYUSB_MAX_SPEED AND ERGOTYPE_TINYUSB_MAX_SPEED STREQUAL "OPT_MODE_HIGH_SPEED")
    list(APPEND ERGOTYPE_TINYUSB_LINK_U
        -Wl,-u,tud_descriptor_device_qualifier_cb
        -Wl,-u,tud_descriptor_other_speed_configuration_cb
    )
endif()
idf_build_set_property(LINK_OPTIONS "${ERGOTYPE_TINYUSB_LINK_U}" APPEND)

idf_build_get_property(build_components BUILD_COMPONENTS)
if(espressif__tinyusb IN_LIST build_components)
    set(ERGOTYPE_TINYUSB_COMPONENT espressif__tinyusb)
elseif(tinyusb IN_LIST build_components)
    set(ERGOTYPE_TINYUSB_COMPONENT tinyusb)
else()
    set(ERGOTYPE_TINYUSB_COMPONENT "")
endif()

if(ERGOTYPE_TINYUSB_COMPONENT)
    idf_component_get_property(ERGOTYPE_TINYUSB_LIB ${ERGOTYPE_TINYUSB_COMPONENT} COMPONENT_LIB)
    target_include_directories(${ERGOTYPE_TINYUSB_LIB} PRIVATE ${ERGOTYPE_ROOT}/tusb)
    if(DEFINED ERGOTYPE_TINYUSB_RHPORT)
        target_compile_definitions(${ERGOTYPE_TINYUSB_LIB} PRIVATE BOARD_TUD_RHPORT=${ERGOTYPE_TINYUSB_RHPORT})
    endif()
    if(DEFINED ERGOTYPE_TINYUSB_MAX_SPEED)
        target_compile_definitions(${ERGOTYPE_TINYUSB_LIB} PRIVATE BOARD_TUD_MAX_SPEED=${ERGOTYPE_TINYUSB_MAX_SPEED})
    endif()
    if(DEFINED ERGOTYPE_USB_COMPOSITE_NKRO_KB_MOUSE)
        target_compile_definitions(${ERGOTYPE_TINYUSB_LIB} PRIVATE ERGOTYPE_USB_COMPOSITE_NKRO_KB_MOUSE=${ERGOTYPE_USB_COMPOSITE_NKRO_KB_MOUSE})
    endif()
endif()

set_property(TARGET ${COMPONENT_LIB} PROPERTY C_STANDARD 11)
