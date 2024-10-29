// This software is distributed under the terms of the MIT License.
// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
// Copyright Amazon.com Inc. or its affiliates.
// SPDX-License-Identifier: MIT
// Author: Sergei Shirokov <sergei.shirokov@zubax.com>

#include "application.hpp"
#include "platform/common_helpers.hpp"
#include "platform/posix/udp/udp_media.hpp"

#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/application/node.hpp>
#include <libcyphal/executor.hpp>
#include <libcyphal/transport/can/can_transport_impl.hpp>
#include <libcyphal/transport/udp/udp_transport_impl.hpp>
#include <libcyphal/types.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>

using namespace std::chrono_literals;

using Callback = libcyphal::IExecutor::Callback;

int main()
{
    std::cout << "LibCyphal demo.\n";

    constexpr std::size_t TxQueueCapacity = 16;

    Application application;
    auto&       executor = application.executor();

    auto iface_params = application.getIfaceParams();

    // 1. Create the transport layer object.
    //
    libcyphal::UniquePtr<libcyphal::transport::udp::IUdpTransport> upd_transport;
    platform::posix::UdpMediaCollection                            udp_media_collection{application.memory(), executor};
    if (!iface_params.udp_iface.value().empty())
    {
        udp_media_collection.parse(iface_params.udp_iface.value());
        auto maybe_udp_transport = makeTransport(  //
            {application.memory()},
            executor,
            udp_media_collection.span(),
            TxQueueCapacity);
        if (const auto* failure = cetl::get_if<libcyphal::transport::FactoryFailure>(&maybe_udp_transport))
        {
            std::cerr << "Failed to create UDP transport (iface='"
                      << static_cast<cetl::string_view>(iface_params.udp_iface.value()) << "').\n";
            return 2;
        }
        upd_transport = cetl::get<libcyphal::UniquePtr<libcyphal::transport::udp::IUdpTransport>>(  //
            std::move(maybe_udp_transport));

        upd_transport->setLocalNodeId(7);
        upd_transport->setTransientErrorHandler(platform::CommonHelpers::Udp::transientErrorReporter);
    }

    // 2. Create the presentation layer object.
    //
    libcyphal::presentation::Presentation presentation{application.memory(), executor, *upd_transport};

    // 3. Create the node object with name.
    //
    auto maybe_node = libcyphal::application::Node::make(presentation);
    if (const auto* failure = cetl::get_if<libcyphal::application::Node::MakeFailure>(&maybe_node))
    {
        std::cerr << "Failed to create node (iface='" << static_cast<cetl::string_view>(iface_params.udp_iface.value())
                  << "').\n";
        return 10;
    }
    auto node = cetl::get<libcyphal::application::Node>(std::move(maybe_node));

    // Main loop.
    //
    libcyphal::Duration        worst_lateness{0};
    const libcyphal::TimePoint deadline = executor.now() + 10s;
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
