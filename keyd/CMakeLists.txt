cmake_minimum_required(VERSION 3.13)

project(keyd_project C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Os")

set(KEYD_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/src")
set(KEYD_PORT_DIR "${CMAKE_CURRENT_LIST_DIR}/port")

include_directories("${CMAKE_CURRENT_LIST_DIR}/../")
include_directories("${CMAKE_CURRENT_LIST_DIR}/../rtos")

add_library(keyd
  ${KEYD_PORT_DIR}/config.c
  ${KEYD_PORT_DIR}/macro.c
  ${KEYD_SRC_DIR}/keys.c
  ${KEYD_SRC_DIR}/string.c
  ${KEYD_PORT_DIR}/keyboard.c
  ${KEYD_PORT_DIR}/evloop.c
  ${KEYD_PORT_DIR}/daemon.c
  ${KEYD_PORT_DIR}/hooks.c
  ${KEYD_PORT_DIR}/task.c
  ${KEYD_SRC_DIR}/unicode.c
  ${KEYD_PORT_DIR}/vkbd/vkbd.c
  ${KEYD_PORT_DIR}/vkbd/tusb_hid.c
)

target_link_libraries(keyd
  PRIVATE
  usbd
  fsd
  pico_stdlib
  FreeRTOS-Kernel
)

# Keep upstream include style without shadowing standard library headers like <string.h>.
target_compile_options(keyd PRIVATE
  -iquote${KEYD_PORT_DIR}
  -iquote${KEYD_SRC_DIR}
)
