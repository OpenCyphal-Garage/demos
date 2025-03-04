# This software is distributed under the terms of the MIT License.
# Copyright (C) OpenCyphal Development Team  <opencyphal.org>
# Copyright Amazon.com Inc. or its affiliates.
# SPDX-License-Identifier: MIT
# Author: Sergei Shirokov <sergei.shirokov@zubax.com>

cmake_minimum_required(VERSION 3.25)

# Define type generation and header library all in one go.
#
set(dsdl_types_in_demo # List all the DSDL types used in the engine
        uavcan/file/405.GetInfo.0.2.dsdl
        uavcan/file/408.Read.1.1.dsdl
        uavcan/node/430.GetInfo.1.0.dsdl
        uavcan/node/435.ExecuteCommand.1.3.dsdl
        uavcan/node/7509.Heartbeat.1.0.dsdl
        uavcan/register/384.Access.1.0.dsdl
        uavcan/register/385.List.1.0.dsdl
)
add_cyphal_library(
        NAME demo
        DSDL_FILES ${dsdl_types_in_demo}
        ALLOW_EXPERIMENTAL_LANGUAGES
        LANGUAGE cpp
        LANGUAGE_STANDARD ${CYPHAL_LANGUAGE_STANDARD}
        OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/dsdl_transpiled
        OUT_LIBRARY_TARGET demo_transpiled
)

# Define the demo application build target and link it with the library.
add_executable(demo
        ${CMAKE_SOURCE_DIR}/src/application.cpp
        ${CMAKE_SOURCE_DIR}/src/main.cpp
        ${CMAKE_SOURCE_DIR}/src/no_cpp_heap.cpp
)

target_link_libraries(demo
        PRIVATE o1heap udpard shared_udp
        PRIVATE ${demo_transpiled}
)
if (${PLATFORM_OS_TYPE} STREQUAL "linux")
    target_link_libraries(demo
            PRIVATE canard shared_socketcan
    )
endif ()

target_include_directories(demo PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(demo PRIVATE ${submodules_dir}/cetl/include)
target_include_directories(demo PRIVATE ${submodules_dir}/libcyphal/include)

if (STATIC_ANALYSIS)
    set_target_properties(demo PROPERTIES C_CLANG_TIDY "${clang_tidy}")
endif ()
