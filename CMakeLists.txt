cmake_minimum_required(VERSION 3.20)

project(egx_quant
    VERSION 0.1.0
    DESCRIPTION "EGX30 futures pricing and EGP risk engine"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(EGXQ_BUILD_TESTS "Build unit tests" ON)
option(EGXQ_BUILD_APPS  "Build command-line apps" ON)

# Strict warnings for our own targets only
add_library(egxq_warnings INTERFACE)
if(MSVC)
    target_compile_options(egxq_warnings INTERFACE /W4 /permissive-)
else()
    target_compile_options(egxq_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
endif()

include(cmake/sqlite3.cmake)
include(cmake/miniz.cmake)

add_subdirectory(src)

if(EGXQ_BUILD_APPS)
    add_subdirectory(apps)
endif()

if(EGXQ_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
