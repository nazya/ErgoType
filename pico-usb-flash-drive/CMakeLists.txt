cmake_minimum_required(VERSION 3.13)

set(PROJECT_NAME fsd)
project(${PROJECT_NAME} C CXX ASM)

# set(PICO_BOARD pico_w)
set(FAMILY rp2040)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

add_library(${PROJECT_NAME}
  ${PICO_SDK_PATH}/lib/tinyusb/lib/fatfs/source/ff.c
  ${PICO_SDK_PATH}/lib/tinyusb/lib/fatfs/source/ffsystem.c
  ${PICO_SDK_PATH}/lib/tinyusb/lib/fatfs/source/ffunicode.c
  ${CMAKE_CURRENT_LIST_DIR}/fatfs_driver.c
  ${CMAKE_CURRENT_LIST_DIR}/flash.c
  # usb_msc_driver.c
  # usb_descriptors.c
)

target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${PICO_SDK_PATH}/lib/tinyusb/lib/fatfs/source
  ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(${PROJECT_NAME}
  PRIVATE

  pico_stdlib
  pico_flash
  # tinyusb_device
  hardware_flash
  hardware_exception
)

#pico_enable_stdio_usb(${PROJECT_NAME} 1)
#pico_add_extra_outputs(${PROJECT_NAME})
