# This software is distributed under the terms of the MIT License.
# Copyright (C) OpenCyphal Development Team  <opencyphal.org>
# Copyright Amazon.com Inc. or its affiliates.
# SPDX-License-Identifier: MIT
# Author: Sergei Shirokov <sergei.shirokov@zubax.com>

cmake_minimum_required(VERSION 3.20)

# Define the demo application build target and link it with the library.
add_library(
        platform
        ${CMAKE_SOURCE_DIR}/src/platform/posix/udp/udp.c
        ${CMAKE_SOURCE_DIR}/src/platform/linux/can/socketcan.c
)
target_link_libraries(platform PUBLIC canard)
target_include_directories(platform PRIVATE ${CMAKE_SOURCE_DIR}/src/platform)
