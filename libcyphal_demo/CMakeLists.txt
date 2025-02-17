# This software is distributed under the terms of the MIT License.
# Copyright (C) OpenCyphal Development Team  <opencyphal.org>
# Copyright Amazon.com Inc. or its affiliates.
# SPDX-License-Identifier: MIT
# Author: Sergei Shirokov <sergei.shirokov@zubax.com>

cmake_minimum_required(VERSION 3.25)

project(libcyphal_demo
        LANGUAGES CXX C
        HOMEPAGE_URL https://github.com/OpenCyphal-Garage/libcyphal)

set(CMAKE_CXX_STANDARD "14" CACHE STRING "C++ standard to use when compiling.")
set(DISABLE_CPP_EXCEPTIONS ON CACHE STRING "Disable C++ exceptions.")

option(CETL_ENABLE_DEBUG_ASSERT "Enable or disable runtime CETL asserts." ON)

set(CXX_FLAG_SET "")
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if (DISABLE_CPP_EXCEPTIONS)
        message(STATUS "DISABLE_CPP_EXCEPTIONS is true. Adding -fno-exceptions to compiler flags.")
        list(APPEND CXX_FLAG_SET "-fno-exceptions")
    endif()
endif()
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # Disable PSABI warnings in GCC (on RPi).
    list(APPEND CXX_FLAG_SET "-Wno-psabi")
endif()
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:${CXX_FLAG_SET}>")

if (CETL_ENABLE_DEBUG_ASSERT)
    add_compile_definitions("CETL_ENABLE_DEBUG_ASSERT=1")
endif()

# Set the output binary directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(submodules_dir "${CMAKE_SOURCE_DIR}/../submodules")

# Set up static analysis.
set(STATIC_ANALYSIS ON CACHE BOOL "enable static analysis")
if (STATIC_ANALYSIS)
    # clang-tidy (separate config files per directory)
    find_program(clang_tidy NAMES clang-tidy)
    if (NOT clang_tidy)
        message(FATAL_ERROR "Could not locate clang-tidy")
    endif ()
    message(STATUS "Using clang-tidy: ${clang_tidy}")
endif ()

# Pull in Nunavut's cmake integration
find_package("Nunavut" 3.0 REQUIRED)

# libcyphal requires PMR support for Nunavut generated code.
if (${CMAKE_CXX_STANDARD} STREQUAL "14")
    set(CYPHAL_LANGUAGE_STANDARD "cetl++14-17")
else ()
    set(CYPHAL_LANGUAGE_STANDARD "c++${CMAKE_CXX_STANDARD}-pmr")
endif ()

# Forward the revision information to the compiler so that we could expose it at runtime. This is entirely optional.
execute_process(
        COMMAND git rev-parse --short=16 HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE vcs_revision_id
        OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(STATUS "vcs_revision_id: ${vcs_revision_id}")
add_definitions(
        -DVERSION_MAJOR=1
        -DVERSION_MINOR=0
        -DVCS_REVISION_ID=0x${vcs_revision_id}ULL
        -DNODE_NAME="org.opencyphal.demos.libcyphal"
)
if (DEFINED PLATFORM_OS_TYPE)
    if (${PLATFORM_OS_TYPE} STREQUAL "bsd")
        add_definitions(-DPLATFORM_OS_TYPE_BSD)
    elseif (${PLATFORM_OS_TYPE} STREQUAL "linux")
        add_definitions(-DPLATFORM_OS_TYPE_LINUX)
    endif ()
endif ()

add_definitions(-DNUNAVUT_ASSERT=assert)

# Define the LibUDPard static library build target.
add_library(udpard STATIC ${submodules_dir}/libudpard/libudpard/udpard.c)
target_include_directories(udpard INTERFACE SYSTEM ${submodules_dir}/libudpard/libudpard)
include(${CMAKE_SOURCE_DIR}/../shared/udp/udp.cmake)

if (${PLATFORM_OS_TYPE} STREQUAL "linux")
    # Define the LibCANard static library build target.
    add_library(canard STATIC ${submodules_dir}/libcanard/libcanard/canard.c)
    target_include_directories(canard INTERFACE SYSTEM ${submodules_dir}/libcanard/libcanard)
    include(${CMAKE_SOURCE_DIR}/../shared/socketcan/socketcan.cmake)
endif ()

# Build o1heap -- a hard real-time deterministic memory allocator for embedded systems.
add_library(o1heap STATIC ${submodules_dir}/o1heap/o1heap/o1heap.c)
target_include_directories(o1heap INTERFACE SYSTEM ${submodules_dir}/o1heap/o1heap)

add_subdirectory(src)
