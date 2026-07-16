# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
set(PICO_BOARD pico CACHE STRING "Board type")

# set(PICO_BOARD waveshare_rp2040_pizero CACHE STRING "Board type" FORCE)

# set(PICO_BOARD pico2 CACHE STRING "Board type" FORCE)

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

# set(FREERTOS_HEAP 2)
set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_SOURCE_DIR}/FreeRTOS-Kernel)
include(${CMAKE_CURRENT_SOURCE_DIR}/rtos/FreeRTOS_Kernel_import.cmake)


include(${CMAKE_CURRENT_SOURCE_DIR}/coreJSON/jsonFilePaths.cmake)

set(PROJECT_NAME ErgoType)
project(${PROJECT_NAME} C CXX ASM)

set(FATFS_FFCONF ${PICO_SDK_PATH}/lib/tinyusb/lib/fatfs/source/ffconf.h)
file(READ ${FATFS_FFCONF} FATFS_FFCONF_CONTENT)
if(NOT FATFS_FFCONF_CONTENT MATCHES "#define[ \t]+FF_USE_STRFUNC[ \t]+1")
    string(REGEX REPLACE "#define[ \t]+FF_USE_STRFUNC[ \t]+[0-9]+"
           "#define FF_USE_STRFUNC 1"
           FATFS_FFCONF_CONTENT
           "${FATFS_FFCONF_CONTENT}")
    file(WRITE ${FATFS_FFCONF} "${FATFS_FFCONF_CONTENT}")
endif()


# Ensure Pico SDK's TinyUSB is built with the FreeRTOS OSAL (CFG_TUSB_OS).
# This must be set before pico_sdk_init() (where tinyusb_device/tinyusb_board targets are created).
set(TINYUSB_OPT_OS OPT_OS_FREERTOS)
set(PICO_PIO_USB_PATH ${CMAKE_CURRENT_SOURCE_DIR}/Pico-PIO-USB)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1
add_executable(${PROJECT_NAME})

pico_generate_pio_header(${PROJECT_NAME}
        ${CMAKE_CURRENT_LIST_DIR}/led/ws2812.pio
)

add_subdirectory(tusb)
add_subdirectory(pico-usb-flash-drive)
add_subdirectory(keyd)
add_subdirectory(pointing/pmw3360)
add_subdirectory(pointing/pmw3389)

target_sources(${PROJECT_NAME} PUBLIC
        ${JSON_SOURCES}
        # ${CMAKE_CURRENT_LIST_DIR}/test.c
        ${CMAKE_CURRENT_LIST_DIR}/main.c
        ${CMAKE_CURRENT_LIST_DIR}/devmon.c
        ${CMAKE_CURRENT_LIST_DIR}/tusb_device_task.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/task.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/usbhid.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/hid_async.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/hid_workqueue.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/hid_timer.c
        # CONFIG_USB_HIDDEV is disabled until the firmware has a hiddev consumer.
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/usbhid/hiddev.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/evdev_client.c
        # Deferred hidraw calls are documented inline in hid-core.c.
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-core.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-input.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-generic.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-drivers.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-quirks.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-accutouch.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-a4tech.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-appleir.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-aureal.c
        # Deferred: HIDDEV-forced driver; keep out until hiddev proxy coverage is
        # hardware/emulator tested for this path.
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-belkin.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-chicony.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-cherry.c
        # Deferred: mixed driver file; CM6533 raw/hiddev path is disabled in
        # source, and HS100B-only enablement should be tested explicitly.
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-cmedia.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-creative-sb0540.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-cypress.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-elecom.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-evision.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-ezkey.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-gembird.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-google-stadiaff.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-gyration.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-glorious.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-huawei.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-holtek-kbd.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-holtek-mouse.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-icade.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-ite.c
        # Deferred: keep Jabra out until this driver has explicit hardware/emulator
        # coverage; the generic pre-probe descriptor path is now available.
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-jabra.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-kensington.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-keytouch.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-kye.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-lcpower.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-macally.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-maltron.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-monterey.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-nti.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-ortek.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-penmount.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-petalynx.c
        # Deferred: HIDDEV-forced audio/headset path; keep out until hiddev proxy
        # behavior is tested for this driver.
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-plantronics.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-primax.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-pxrc.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-razer.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-rapoo.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-redragon.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-retrode.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-saitek.c
        # Deferred: Samsung IrDA 184-byte path forces HIDDEV instead of normal
        # hidinput; keep out until firmware has a useful hiddev consumer/proxy.
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-samsung.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-semitek.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-sigmamicro.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-speedlink.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-sunplus.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-tivo.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-topre.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-topseed.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-twinhan.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-viewsonic.c
        # Deferred: Vivaldi keyboard stack needs the matching fmap/input support
        # path enabled and tested as a unit.
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-vivaldi-common.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-vivaldi.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-vrc2.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-waltop.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-xiaomi.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-xinmo.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-zydacron.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/input/input.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/evdev.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/input/input-mt.c
        # Deferred with hid-vivaldi*.c above.
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/input/vivaldi-fmap.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/input/ff-core.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/input/ff-memless.c
        # Deferred: LED/backlight class proxies are not needed by the current HID
        # allowlist.
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/leds/led-class.c
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/video/backlight/backlight.c
        # Deferred: CONFIG_HID_BATTERY_STRENGTH is disabled until the
        # power_supply proxy has matching driver/hardware coverage.
        # ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/power/supply/power_supply_core.c
        ${CMAKE_CURRENT_LIST_DIR}/rtos/freertos_hook.c
        ${CMAKE_CURRENT_LIST_DIR}/led/led.c
        ${CMAKE_CURRENT_LIST_DIR}/led/ws2812.c
        ${CMAKE_CURRENT_LIST_DIR}/keyscan.c
        ${CMAKE_CURRENT_LIST_DIR}/jconfig.c
        ${CMAKE_CURRENT_LIST_DIR}/log.c
        ${CMAKE_CURRENT_LIST_DIR}/stdio_tusb_cdc.c
        ${CMAKE_CURRENT_LIST_DIR}/uart_stdio.c
        ${CMAKE_CURRENT_LIST_DIR}/display/ssd1306.c
        ${CMAKE_CURRENT_LIST_DIR}/ui/ui.c
        ${CMAKE_CURRENT_LIST_DIR}/accel/filter.c
        ${CMAKE_CURRENT_LIST_DIR}/accel/filter-adaptive.c
        ${CMAKE_CURRENT_LIST_DIR}/accel/filter-adaptive-high-dpi.c
        ${CMAKE_CURRENT_LIST_DIR}/accel/filter-adaptive-low-dpi.c
        ${CMAKE_CURRENT_LIST_DIR}/accel/filter-custom.c
        ${CMAKE_CURRENT_LIST_DIR}/accel/filter-flat.c
        ${CMAKE_CURRENT_LIST_DIR}/pointing/pointer.c
        ${CMAKE_CURRENT_LIST_DIR}/tusb/usb_cdc_callbacks.c
        ${CMAKE_CURRENT_LIST_DIR}/tusb/usb_device_callbacks.c
        ${CMAKE_CURRENT_LIST_DIR}/tusb/usb_hid_callbacks.c
        ${CMAKE_CURRENT_LIST_DIR}/tusb/usb_webhid.c
        ${CMAKE_CURRENT_LIST_DIR}/tusb/usb_msc_callbacks.c
)

# Make sure TinyUSB can find tusb_config.h
target_include_directories(${PROJECT_NAME} PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/rtos
        ${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/include
        ${JSON_INCLUDE_PUBLIC_DIRS}
)

# Expose keyd headers to this target via quote-only search path so system
# headers like <string.h> are never shadowed by keyd/src/string.h.
target_compile_options(${PROJECT_NAME} PRIVATE
        -iquote${CMAKE_CURRENT_LIST_DIR}/keyd/port
        -iquote${CMAKE_CURRENT_LIST_DIR}/keyd/src
)

# In addition to pico_stdlib required for common PicoSDK functionality, add dependency on tinyusb_device
# for TinyUSB device support and tinyusb_board for the additional board support library used by the example
target_link_libraries(${PROJECT_NAME} PUBLIC
        tusb_glue
        flash_fatfs
        keyd
        pmw3360
        pmw3389

        pico_async_context_freertos
        FreeRTOS-Kernel
        FreeRTOS-Kernel-Heap4

        pico_stdlib
        pico_flash

        pico_unique_id
        pico_rand
        tinyusb_additions
        tinyusb_device
        tinyusb_host
        tinyusb_pico_pio_usb
        tinyusb_board

        hardware_sync
        hardware_flash
        hardware_i2c
        hardware_pio
        hardware_clocks

        # pico_cyw43_arch_none
)



# Uncomment this line to enable fix for Errata RP2040-E5 (the fix requires use of GPIO 15)
#target_compile_definitions(${PROJECT_NAME} PUBLIC PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1)

pico_add_extra_outputs(${PROJECT_NAME})

target_compile_definitions(${PROJECT_NAME} PRIVATE
        PICO_STDIO_STACK_BUFFER_SIZE=256 # use a small printf on stack buffer
        PICO_STDIO_USB_STDOUT_TIMEOUT_US=5000000 # avoid discarding long CDC bursts under load
        PICO_DEFAULT_PIO_USB_DP_PIN=21
)

# add url via pico_set_program_url
pico_enable_stdio_usb(${PROJECT_NAME} 0)
pico_enable_stdio_uart(${PROJECT_NAME} 1)
