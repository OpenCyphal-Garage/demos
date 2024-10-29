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
#include <libcyphal/application/registry/registry.hpp>
#include <libcyphal/executor.hpp>
#include <libcyphal/transport/can/can_transport_impl.hpp>
#include <libcyphal/transport/udp/udp_transport_impl.hpp>
#include <libcyphal/types.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>

using namespace std::chrono_literals;

using Callback = libcyphal::IExecutor::Callback;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

namespace
{

/// Helper function to create a register string value.
///
libcyphal::application::registry::IRegister::Value makeStringValue(cetl::pmr::memory_resource& memory,
                                                                   const cetl::string_view     sv)
{
    using Value = libcyphal::application::registry::IRegister::Value;

    const Value::allocator_type alloc{&memory};
    Value                       value{alloc};

    auto& str = value.set_string();
    std::copy(sv.begin(), sv.end(), std::back_inserter(str.value));

    return value;
}

}  // namespace

int main()
{
    std::cout << "LibCyphal demo.\n";

    constexpr std::size_t TxQueueCapacity = 16;

    Application application;
    auto&       memory   = application.memory();
    auto&       executor = application.executor();

    auto iface_params = application.getIfaceParams();

    // 1. Create the transport layer object.
    //
    libcyphal::UniquePtr<libcyphal::transport::udp::IUdpTransport> upd_transport;
    platform::posix::UdpMediaCollection                            udp_media_collection{memory, executor};
    if (!iface_params.udp_iface.value().empty())
    {
        udp_media_collection.parse(iface_params.udp_iface.value());
        auto maybe_udp_transport = makeTransport({memory}, executor, udp_media_collection.span(), TxQueueCapacity);
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
    libcyphal::presentation::Presentation presentation{memory, executor, *upd_transport};

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

    // 4. Populate the node info.
    //
    // The hardware version is not populated in this demo because it runs on no specific hardware.
    // An embedded node would usually determine the version by querying the hardware.
    auto& get_info                    = node.getInfoProvider().response();
    get_info.software_version.major   = VERSION_MAJOR;
    get_info.software_version.minor   = VERSION_MINOR;
    get_info.software_vcs_revision_id = VCS_REVISION_ID;
    //
    const cetl::string_view node_name{NODE_NAME};
    get_info.name.resize(node_name.size());
    (void) std::memmove(get_info.name.data(), node_name.data(), get_info.name.size());

    // 5. Bring up registry provider, and expose several registers.
    //
    if (const auto failure = node.makeRegistryProvider(application.registry()))
    {
        std::cerr << "Failed to create registry provider.\n";
        return 11;
    }
    // Expose `get_info.name` as mutable node description.
    auto reg_node_desc = application.registry().route(  //
        "uavcan.node.description",
        [&memory, &get_info] {
            //
            return makeStringValue(memory, libcyphal::application::registry::makeStringView(get_info.name));
        },
        [&get_info](const auto& value) {
            //
            if (const auto* const str = value.get_string_if())
            {
                get_info.name = str->value;
            }
            return cetl::nullopt;
        });

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

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
