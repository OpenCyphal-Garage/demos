# This software is distributed under the terms of the MIT License.
# Copyright (C) OpenCyphal Development Team  <opencyphal.org>
# Copyright Amazon.com Inc. or its affiliates.
# SPDX-License-Identifier: MIT
# Author: Sergei Shirokov <sergei.shirokov@zubax.com>

cmake_minimum_required(VERSION 3.20)

# Define the demo application build target and link it with the library.
add_library(
        shared_register
        ${CMAKE_CURRENT_LIST_DIR}/register.c
)
add_dependencies(shared_register dsdl_uavcan)
target_include_directories(shared_register PUBLIC ${CMAKE_CURRENT_LIST_DIR})
