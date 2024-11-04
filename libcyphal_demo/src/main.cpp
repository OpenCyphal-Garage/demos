// This software is distributed under the terms of the MIT License.
// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
// Copyright Amazon.com Inc. or its affiliates.
// SPDX-License-Identifier: MIT
// Author: Sergei Shirokov <sergei.shirokov@zubax.com>

#include "application.hpp"
#include "exec_cmd_provider.hpp"
#include "transport_bag_can.hpp"
#include "transport_bag_udp.hpp"

#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/application/node.hpp>
#include <libcyphal/presentation/presentation.hpp>
#include <libcyphal/transport/transport.hpp>
#include <libcyphal/types.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <unistd.h>  // execve
#include <utility>

using namespace std::chrono_literals;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

namespace
{

class AppExecCmdProvider final : public ExecCmdProvider<AppExecCmdProvider>
{
public:
    using ExecCmdProvider::ExecCmdProvider;

    bool should_break() const noexcept
    {
        return should_power_off_ || restart_required_;
    }

    bool should_power_off() const noexcept
    {
        return should_power_off_;
    }

private:
    bool onCommand(const Request::_traits_::TypeOf::command command,
                   const cetl::string_view                  parameter,
                   Response&                                response) noexcept override
    {
        response.status = Response::STATUS_SUCCESS;

        switch (command)
        {
        case Request::COMMAND_POWER_OFF:
            //
            std::cout << "🛑 COMMAND_POWER_OFF\n";
            should_power_off_ = true;
            break;

        case Request::COMMAND_RESTART:
            //
            std::cout << "♻️ COMMAND_RESTART\n";
            restart_required_ = true;
            break;

        case Request::COMMAND_IDENTIFY:
            //
            std::cout << "🔔 COMMAND_IDENTIFY\n";
            break;

        case Request::COMMAND_STORE_PERSISTENT_STATES:
            //
            std::cout << "💾 COMMAND_STORE_PERSISTENT_STATES\n";
            restart_required_ = true;
            break;

        default:
            return ExecCmdProvider::onCommand(command, parameter, response);
        }
        return true;
    }

    bool should_power_off_{false};
    bool restart_required_{false};

};  // AppExecCmdProvider

libcyphal::Expected<bool, int> run_application()
{
    std::cout << "\n🟢 ***************** LibCyphal demo *******************\n";

    Application application;
    auto&       memory   = application.memory();
    auto&       executor = application.executor();

    auto node_params  = application.getNodeParams();
    auto iface_params = application.getIfaceParams();

    // 1. Create the transport layer object. First try CAN, then UDP.
    //
    TransportBagCan transport_bag_can{memory, executor};
    TransportBagUdp transport_bag_udp{memory, executor};
    //
    libcyphal::transport::ITransport* transport_iface = transport_bag_can.create(iface_params);
    if (transport_iface == nullptr)
    {
        transport_iface = transport_bag_udp.create(iface_params);
    }
    if (transport_iface == nullptr)
    {
        std::cerr << "❌ Failed to create any transport.\n";
        return 1;
    }
    (void) transport_iface->setLocalNodeId(node_params.id.value()[0]);
    std::cout << "Node ID   : " << transport_iface->getLocalNodeId().value_or(65535) << "\n";
    std::cout << "Node Name : '" << node_params.description.value().c_str() << "'\n";

    // 2. Create the presentation layer object.
    //
    libcyphal::presentation::Presentation presentation{memory, executor, *transport_iface};

    // 3. Create the node object with name.
    //
    auto maybe_node = libcyphal::application::Node::make(presentation);
    if (const auto* failure = cetl::get_if<libcyphal::application::Node::MakeFailure>(&maybe_node))
    {
        std::cerr << "❌ Failed to create node (iface='"
                  << static_cast<cetl::string_view>(iface_params.udp_iface.value()) << "').\n";
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
    get_info.name.resize(node_params.description.value().size());
    (void) std::memmove(get_info.name.data(), node_params.description.value().data(), get_info.name.size());
    //
    application.getUniqueId(get_info.unique_id);

    // 5. Bring up registry provider.
    //
    if (const auto failure = node.makeRegistryProvider(application.registry()))
    {
        std::cerr << "❌ Failed to create registry provider.\n";
        return 11;
    }

    // 6. Bring up the command execution provider.
    //
    auto maybe_exec_cmd_provider = AppExecCmdProvider::make(presentation);
    if (const auto* failure = cetl::get_if<libcyphal::application::Node::MakeFailure>(&maybe_node))
    {
        std::cerr << "❌ Failed to create exec cmd provider.\n";
        return 12;
    }
    auto exec_cmd_provider = cetl::get<AppExecCmdProvider>(std::move(maybe_exec_cmd_provider));

    // Main loop.
    //
    libcyphal::Duration worst_lateness{0};
    std::cout << "-----------\nRunning..." << std::endl;  // NOLINT
    //
    while (!exec_cmd_provider.should_break())
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
    std::cout << "🏁 Done.\n-----------\nRun Stats:\n";
    std::cout << "  worst_callback_lateness=" << worst_lateness.count() << "us\n";

    return !exec_cmd_provider.should_power_off();
}

}  // namespace

int main(const int, char* const argv[])
{
    const auto result = run_application();
    if (const auto* const err = cetl::get_if<int>(&result))
    {
        return *err;
    }

    if (cetl::get<bool>(result))
    {
        return -::execve(argv[0], argv, ::environ);  // NOLINT
    }
    return 0;
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
