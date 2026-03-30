cmake_minimum_required(VERSION 3.12)

set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in SDK (must set be before project)
include(pico_sdk_import.cmake)

project(densha_pico C CXX ASM)

set(CMAKE_C_STANDARD 11)

pico_sdk_init()

include_directories(modules/aic_pico/firmware/include)

add_subdirectory(src)
