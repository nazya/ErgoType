set(PROJECT_NAME usbd)
project(${PROJECT_NAME} C CXX ASM)

# include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)


# Create a static library for USB functionalities using the project's name
add_library(${PROJECT_NAME} SHARED usb_descriptors.c usb_descriptors.h tusb_config.h)

# Publicly include the current source directory for the library
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC pico_stdlib pico_unique_id tinyusb_device tinyusb_board)

