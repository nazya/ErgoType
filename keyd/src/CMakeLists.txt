cmake_minimum_required(VERSION 3.13)

project(keyd_project C CXX ASM)

set(PICO_BOARD pico_w)
set(FAMILY rp2040)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Os")

include_directories("${CMAKE_CURRENT_LIST_DIR}/../../")

add_library(keyd
  ${CMAKE_CURRENT_LIST_DIR}/config.c
  ${CMAKE_CURRENT_LIST_DIR}/ini.c
  ${CMAKE_CURRENT_LIST_DIR}/macro.c
  ${CMAKE_CURRENT_LIST_DIR}/keys.c
  ${CMAKE_CURRENT_LIST_DIR}/stringutils.c
  ${CMAKE_CURRENT_LIST_DIR}/porting.c
  ${CMAKE_CURRENT_LIST_DIR}/keyboard.c
  ${CMAKE_CURRENT_LIST_DIR}/daemon.c
)

target_include_directories(keyd
  PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(keyd
  PRIVATE

  usbd
  fsd

  pico_stdlib
  FreeRTOS-Kernel

)