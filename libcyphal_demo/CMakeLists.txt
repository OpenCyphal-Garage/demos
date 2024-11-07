# This software is distributed under the terms of the MIT License.
# Copyright (C) OpenCyphal Development Team  <opencyphal.org>
# Copyright Amazon.com Inc. or its affiliates.
# SPDX-License-Identifier: MIT
# Author: Sergei Shirokov <sergei.shirokov@zubax.com>

cmake_minimum_required(VERSION 3.20)

project(libcyphal_demo
        LANGUAGES CXX C
        HOMEPAGE_URL https://github.com/OpenCyphal-Garage/libcyphal)

set(CMAKE_CXX_STANDARD "14" CACHE STRING "C++ standard to use when compiling.")
set(DISABLE_CPP_EXCEPTIONS ON CACHE STRING "Disable C++ exceptions.")

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if (DISABLE_CPP_EXCEPTIONS)
        message(STATUS "DISABLE_CPP_EXCEPTIONS is true. Adding -fno-exceptions to compiler flags.")
        list(APPEND CXX_FLAG_SET "-fno-exceptions")
    endif()
endif()

add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:${CXX_FLAG_SET}>")

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")
set(submodules "${CMAKE_SOURCE_DIR}/../submodules")

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

# Transpile DSDL into C++ using Nunavut. Install Nunavut as follows: pip install nunavut.
# Alternatively, you can invoke the transpiler manually or use https://nunaweb.opencyphal.org.
find_package(nnvg REQUIRED)
create_dsdl_target(             # Generate the support library for generated C++ headers, which is "nunavut.h".
        "nunavut_support"
        cpp
        ${CMAKE_BINARY_DIR}/transpiled
        ""
        OFF
        little
        "only"
)
set(dsdl_root_namespace_dirs                # List all DSDL root namespaces to transpile here.
        ${submodules}/public_regulated_data_types/uavcan
)
foreach (ns_dir ${dsdl_root_namespace_dirs})
    get_filename_component(ns ${ns_dir} NAME)
    message(STATUS "DSDL namespace ${ns} at ${ns_dir}")
    create_dsdl_target(
            "dsdl_${ns}"                    # CMake target name
            cpp                             # Target language to transpile into
            ${CMAKE_BINARY_DIR}/transpiled  # Destination directory (add it to the includes)
            ${ns_dir}                       # Source directory
            OFF                             # Disable variable array capacity override
            little                          # Endianness of the target platform (alternatives: "big", "any")
            "never"                         # Support files are generated once in the nunavut_support target (above)
            ${dsdl_root_namespace_dirs}     # Look-up DSDL namespaces
    )
    add_dependencies("dsdl_${ns}" nunavut_support)
endforeach ()
include_directories(SYSTEM ${CMAKE_BINARY_DIR}/transpiled)  # Make the transpiled headers available for inclusion.
add_definitions(-DNUNAVUT_ASSERT=assert)

# Define the LibUDPard static library build target.
add_library(udpard STATIC ${submodules}/libudpard/libudpard/udpard.c)
target_include_directories(udpard INTERFACE SYSTEM ${submodules}/libudpard/libudpard)

# Define the LibCANard static library build target.
add_library(canard STATIC ${submodules}/libcanard/libcanard/canard.c)
target_include_directories(canard INTERFACE SYSTEM ${submodules}/libcanard/libcanard)

# Build o1heap -- a hard real-time deterministic memory allocator for embedded systems.
add_library(o1heap STATIC ${submodules}/o1heap/o1heap/o1heap.c)
target_include_directories(o1heap INTERFACE SYSTEM ${submodules}/o1heap/o1heap)

add_subdirectory(src)
