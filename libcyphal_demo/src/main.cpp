// This software is distributed under the terms of the MIT License.
// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
// Copyright Amazon.com Inc. or its affiliates.
// SPDX-License-Identifier: MIT
// Author: Sergei Shirokov <sergei.shirokov@zubax.com>

#include "application.hpp"

#include <libcyphal/application/node.hpp>

#include <iostream>

int main()
{
    const Application application;

    const std::string str{"LibCyphal demo."};
    std::cout << str << "\n";
    return 0;
}
