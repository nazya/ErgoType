cmake_minimum_required(VERSION 3.13)

project(keyd_project C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Os")

add_library(pmw3389
  ${CMAKE_CURRENT_LIST_DIR}/srom.c
  ${CMAKE_CURRENT_LIST_DIR}/pmw3389.c
)

target_include_directories(pmw3389
  PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/../..
  ${CMAKE_CURRENT_LIST_DIR}/../../rtos
)

target_link_libraries(pmw3389
  PRIVATE
  pico_stdlib
  FreeRTOS-Kernel

  PUBLIC
  hardware_spi
)
