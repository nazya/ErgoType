cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET esp32s3 CACHE STRING "IDF target")
set(COMPONENTS main)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/partition_table")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(ErgoTypeESP C CXX ASM)
