cmake_minimum_required(VERSION 3.9)

project(
    valhalla
    LANGUAGES C
    VERSION 0.0.1
    DESCRIPTION "Valhalla: A C Web Framework"
)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build)
set(CMAKE_C_STANDARD 99)
set(CFLAGS "-Wall -Werror")

add_subdirectory(src)
