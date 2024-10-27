# == DO NEVER EDIT THE NEXT LINES for Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.0.0)
set(toolchainVersion 13_2_Rel1)
set(picotoolVersion 2.0.0)
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

set(PICO_BOARD pico CACHE STRING "Board type")
set(FAMILY rp2040)

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

# set(FREERTOS_HEAP 2)
SET(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_SOURCE_DIR}/FreeRTOS-Kernel)
include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)


include(${CMAKE_CURRENT_SOURCE_DIR}/coreJSON/jsonFilePaths.cmake)

set(PROJECT_NAME ErgoType)
project(${PROJECT_NAME} C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1
cmake_minimum_required(VERSION 3.13)
add_executable(${PROJECT_NAME})
# target_compile_definitions(${PROJECT_NAME} PUBLIC FF_USE_STRFUNC=1)
# add_definitions(-DFF_USE_STRFUNC=1)
# add_compile_definitions(FF_USE_STRFUNC=1)

add_subdirectory(tusb)
add_subdirectory(pico-usb-flash-drive)
add_subdirectory(keyd/src)

target_sources(${PROJECT_NAME} PUBLIC
        ${JSON_SOURCES}
        ${CMAKE_CURRENT_LIST_DIR}/main.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_msc_driver.c
        ${CMAKE_CURRENT_LIST_DIR}/freertos_hook.c
        ${CMAKE_CURRENT_LIST_DIR}/led.c
        ${CMAKE_CURRENT_LIST_DIR}/tud.c
        ${CMAKE_CURRENT_LIST_DIR}/keyscan.c
        ${CMAKE_CURRENT_LIST_DIR}/jconfig.c
        )

# Make sure TinyUSB can find tusb_config.h
target_include_directories(${PROJECT_NAME} PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
        ${JSON_INCLUDE_PUBLIC_DIRS}
) 

# In addition to pico_stdlib required for common PicoSDK functionality, add dependency on tinyusb_device
# for TinyUSB device support and tinyusb_board for the additional board support library used by the example
target_link_libraries(${PROJECT_NAME} PUBLIC 
        usbd
        fsd
        keyd

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

        # pico_cyw43_arch_none
   )



# Uncomment this line to enable fix for Errata RP2040-E5 (the fix requires use of GPIO 15)
#target_compile_definitions(${PROJECT_NAME} PUBLIC PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1)

pico_add_extra_outputs(${PROJECT_NAME})

target_compile_definitions(${PROJECT_NAME} PRIVATE
        PICO_STDIO_STACK_BUFFER_SIZE=256 # use a small printf on stack buffer
)

# add url via pico_set_program_url
pico_enable_stdio_usb(${PROJECT_NAME} 1)
