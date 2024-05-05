# This software is distributed under the terms of the MIT License.
# Copyright (C) 2021 OpenCyphal <consortium@opencyphal.org>
# Author: Sergei Shirokov <sergei.shirokov@zubax.com>

cmake_minimum_required(VERSION 3.20)
project(libcyphal_demo C CXX)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")

set(submodules "${CMAKE_SOURCE_DIR}/../submodules")

# Set up static analysis.
set(STATIC_ANALYSIS OFF CACHE BOOL "enable static analysis")
if (STATIC_ANALYSIS)
    # clang-tidy (separate config files per directory)
    find_program(clang_tidy NAMES clang-tidy)
    if (NOT clang_tidy)
        message(FATAL_ERROR "Could not locate clang-tidy")
    endif ()
    message(STATUS "Using clang-tidy: ${clang_tidy}")
endif ()

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -pedantic -fstrict-aliasing")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wdouble-promotion -Wswitch-enum -Wfloat-equal -Wundef")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wconversion -Wtype-limits")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wsign-conversion -Wcast-align -Wmissing-declarations")

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

# Transpile DSDL into C using Nunavut. Install Nunavut as follows: pip install nunavut.
# Alternatively, you can invoke the transpiler manually or use https://nunaweb.opencyphal.org.
find_package(nnvg REQUIRED)
create_dsdl_target(             # Generate the support library for generated C headers, which is "nunavut.h".
        "nunavut_support"
        c
        ${CMAKE_BINARY_DIR}/transpiled
        ""
        OFF
        little
        "only"
)
set(dsdl_root_namespace_dirs                # List all DSDL root namespaces to transpile here.
        ${submodules}/public_regulated_data_types/uavcan
        ${submodules}/public_regulated_data_types/reg
)
foreach (ns_dir ${dsdl_root_namespace_dirs})
    get_filename_component(ns ${ns_dir} NAME)
    message(STATUS "DSDL namespace ${ns} at ${ns_dir}")
    create_dsdl_target(
            "dsdl_${ns}"                    # CMake target name
            c                               # Target language to transpile into
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

# Build libcanard.
add_library(canard STATIC ${submodules}/libcanard/libcanard/canard.c)
include_directories(SYSTEM ${submodules}/libcanard/libcanard)

# Build o1heap -- a hard real-time deterministic memory allocator for embedded systems.
add_library(o1heap STATIC ${submodules}/o1heap/o1heap/o1heap.c)
include_directories(SYSTEM ${submodules}/o1heap/o1heap/)

# Build socketcan.
add_library(socketcan STATIC ${submodules}/platform_specific_components/socketcan/libcanard/src/socketcan.c)
include_directories(SYSTEM ${submodules}/platform_specific_components/socketcan/libcanard/src)

# Reference CETL & libcyphal.
include_directories(SYSTEM ${submodules}/cetl/include)
include_directories(SYSTEM ${submodules}/libcyphal/include)

# Build the application.
add_executable(libcyphal_demo
        src/main.cpp
        src/can/socket_can_media.cpp
        src/can/socket_can_media.hpp
        src/multiplexer.hpp
        src/monotonic_clock.cpp
)
add_dependencies(libcyphal_demo dsdl_uavcan dsdl_reg)
target_link_libraries(libcyphal_demo canard o1heap socketcan)

if (STATIC_ANALYSIS)
    set_target_properties(libcyphal_demo PROPERTIES CXX_CLANG_TIDY "${clang_tidy}")
endif()