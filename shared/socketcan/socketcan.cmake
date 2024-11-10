# This software is distributed under the terms of the MIT License.
# Copyright (C) OpenCyphal Development Team  <opencyphal.org>
# Copyright Amazon.com Inc. or its affiliates.
# SPDX-License-Identifier: MIT
# Author: Sergei Shirokov <sergei.shirokov@zubax.com>

cmake_minimum_required(VERSION 3.20)

# Define the demo application build target and link it with the library.
add_library(
        shared_socketcan
        ${CMAKE_CURRENT_LIST_DIR}/socketcan.c
)
target_link_libraries(shared_socketcan PUBLIC canard)
target_include_directories(shared_socketcan PUBLIC ${CMAKE_CURRENT_LIST_DIR})
