// This software is distributed under the terms of the MIT License.
// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
// Copyright Amazon.com Inc. or its affiliates.
// SPDX-License-Identifier: MIT
// Author: Sergei Shirokov <sergei.shirokov@zubax.com>

#include "application.hpp"

#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/application/node.hpp>
#include <libcyphal/executor.hpp>
#include <libcyphal/types.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>

using namespace std::chrono_literals;

using Callback = libcyphal::IExecutor::Callback;

int main()
{
    Application application;
    auto&       executor = application.executor();

    std::cout << "LibCyphal demo." << "\n";

    auto cb = executor.registerCallback([](auto&) {
        //
        std::cout << "Callback fired." << "\n";
    });
    cb.schedule(Callback::Schedule::Repeat{executor.now(), libcyphal::Duration{1s}});

    // Main loop.
    //
    libcyphal::Duration        worst_lateness{0};
    const libcyphal::TimePoint deadline = executor.now() + 4s;
    std::cout << "-----------\nRunning..." << std::endl;  // NOLINT
    //
    while (executor.now() < deadline)
    {
        const auto spin_result = executor.spinOnce();
        worst_lateness         = std::max(worst_lateness, spin_result.worst_lateness);

        libcyphal::Duration timeout{1s};  // awake at least once per second
        if (spin_result.next_exec_time.has_value())
        {
            timeout = std::min(timeout, spin_result.next_exec_time.value() - executor.now());
        }
        (void) executor.pollAwaitableResourcesFor(cetl::make_optional(timeout));
    }
    //
    std::cout << "Done.\n-----------\nRun Stats:\n";
    std::cout << "  worst_callback_lateness=" << worst_lateness.count() << "us\n";

    return 0;
}
