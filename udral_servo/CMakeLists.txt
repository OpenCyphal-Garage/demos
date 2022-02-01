# This software is distributed under the terms of the MIT License.
# Copyright (C) 2021 UAVCAN Consortium <consortium@uavcan.org>
# Author: Pavel Kirienko <pavel@uavcan.org>

cmake_minimum_required(VERSION 3.17)
project(udral_servo_demo C CXX)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")

set(submodules "${CMAKE_SOURCE_DIR}/../submodules")

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
        -DNODE_NAME="org.uavcan.demos.udral.servo"
)

# Transpile DSDL into C using Nunavut. Install Nunavut as follows: pip install nunavut.
# Alternatively, you can invoke the transpiler manually or use https://nunaweb.uavcan.org.
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

# Build the application.
add_executable(udral_servo_demo
        src/main.c
        src/register.c
        src/socketcan.c
)
add_dependencies(udral_servo_demo dsdl_uavcan dsdl_reg)
target_link_libraries(udral_servo_demo canard o1heap)