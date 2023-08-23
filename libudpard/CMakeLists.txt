# This software is distributed under the terms of the MIT License.
# Copyright (C) OpenCyphal Development Team  <opencyphal.org>
# Copyright Amazon.com Inc. or its affiliates.
# SPDX-License-Identifier: MIT
# Author: Pavel Kirienko <pavel@opencyphal.org>

cmake_minimum_required(VERSION 3.20)

project(libudpard_demo C)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")
set(submodules "${CMAKE_SOURCE_DIR}/../submodules")

# Set up static analysis.
set(NO_STATIC_ANALYSIS OFF CACHE BOOL "disable static analysis")
if (NOT NO_STATIC_ANALYSIS)
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
        -DNODE_NAME="org.opencyphal.demos.libudpard"
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

# Define the LibUDPard static library build target. No special options are needed to use the library, it's very simple.
add_library(udpard_demo STATIC ${submodules}/libudpard/libudpard/udpard.c)
target_include_directories(udpard_demo INTERFACE SYSTEM ${submodules}/libudpard/libudpard)

# Define the demo application build target and link it with the library.
add_executable(demo ${CMAKE_SOURCE_DIR}/src/main.c ${CMAKE_SOURCE_DIR}/src/storage.c)
target_link_libraries(demo PRIVATE udpard_demo)
add_dependencies(demo dsdl_uavcan dsdl_reg)
set_target_properties(
        demo
        PROPERTIES
        COMPILE_FLAGS "-Wall -Wextra -Werror -pedantic -Wdouble-promotion -Wswitch-enum -Wfloat-equal \
            -Wundef -Wconversion -Wtype-limits -Wsign-conversion -Wcast-align -Wmissing-declarations"
        C_STANDARD 11
        C_EXTENSIONS OFF
        C_CLANG_TIDY "${clang_tidy}"
)
