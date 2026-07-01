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
        ${CMAKE_CURRENT_LIST_DIR}/tusb_device_task.c
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
        tinyusb_additions
        tinyusb_device
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
)

# add url via pico_set_program_url
pico_enable_stdio_usb(${PROJECT_NAME} 0)
pico_enable_stdio_uart(${PROJECT_NAME} 1)
