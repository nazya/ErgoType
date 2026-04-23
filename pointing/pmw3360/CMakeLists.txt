cmake_minimum_required(VERSION 3.13)

project(keyd_project C CXX ASM)

# set(PICO_BOARD pico_w)
# set(FAMILY rp2040)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Os")

include_directories("${CMAKE_CURRENT_LIST_DIR}/../../")

add_library(pmw3360
  ${CMAKE_CURRENT_LIST_DIR}/srom.c
  ${CMAKE_CURRENT_LIST_DIR}/pmw3360.c
)

target_include_directories(pmw3360
  PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
)

# target_link_libraries(pico_stdlib hardware_spi tinyusb_device tinyusb_board)

target_link_libraries(pmw3360
  PRIVATE

  # usbd
  # fsd
  # FreeRTOS-Kernel
  pico_stdlib
  hardware_spi
  # tinyusb_device
  # tinyusb_board
)
