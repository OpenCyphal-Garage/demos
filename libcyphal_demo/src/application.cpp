// This software is distributed under the terms of the MIT License.
// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
// Copyright Amazon.com Inc. or its affiliates.
// SPDX-License-Identifier: MIT
// Author: Sergei Shirokov <sergei.shirokov@zubax.com>

#include "application.hpp"

#include <libcyphal/application/registry/registry_impl.hpp>
#include <libcyphal/platform/storage.hpp>

#include <cetl/pf17/cetlpf.hpp>
#include <o1heap.h>

#include <uavcan/node/GetInfo_1_0.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>  // for std::stoul

namespace
{

constexpr std::size_t HeapSize = 16ULL * 1024ULL;
alignas(O1HEAP_ALIGNMENT) std::array<cetl::byte, HeapSize> s_heap_arena{};

}  // namespace

Application::Application()
    : o1_heap_mr_{s_heap_arena}
    , storage_{"/tmp/" NODE_NAME}
    , registry_{o1_heap_mr_}
    , regs_{registry_}
{
    load(storage_, registry_);

    // Maybe override some of the registry values with environment variables.
    //
    auto iface_params = getIfaceParams();
    if (const auto* const iface_addresses_str = std::getenv("CYPHAL__UDP__IFACE"))
    {
        iface_params.udp_iface.value() = iface_addresses_str;
    }
    if (const auto* const iface_addresses_str = std::getenv("CYPHAL__CAN__IFACE"))
    {
        iface_params.can_iface.value() = iface_addresses_str;
    }
    auto node_params = getNodeParams();
    if (const auto* const node_id_str = std::getenv("CYPHAL__NODE__ID"))
    {
        node_params.id.value()[0] = static_cast<std::uint16_t>(std::stoul(node_id_str));
    }
}

Application::~Application()
{
    save(storage_, registry_);

    const auto mr_diag = o1_heap_mr_.queryDiagnostics();
    std::cout << "O(1) Heap diagnostics:" << "\n"
              << "  capacity=" << mr_diag.capacity << "\n"
              << "  allocated=" << mr_diag.allocated << "\n"
              << "  peak_allocated=" << mr_diag.peak_allocated << "\n"
              << "  peak_request_size=" << mr_diag.peak_request_size << "\n"
              << "  oom_count=" << mr_diag.oom_count << "\n";
}

/// Returns the 128-bit unique-ID of the local node. This value is used in `uavcan.node.GetInfo.Response`.
///
void Application::getUniqueId(uavcan::node::GetInfo::Response_1_0::_traits_::TypeOf::unique_id& out)
{
    using unique_id = uavcan::node::GetInfo::Response_1_0::_traits_::TypeOf::unique_id;

    const auto result = storage_.get(".unique_id", out);
    if (cetl::get_if<libcyphal::platform::storage::Error>(&result) != nullptr)
    {
        std::random_device                          rd;           // Seed for the random number engine
        std::mt19937                                gen(rd());    // Mersenne Twister engine
        std::uniform_int_distribution<std::uint8_t> dis(0, 255);  // Distribution range for bytes

        // Populate the default; it is only used at the first run.
        for (auto& b : out)
        {
            b = dis(gen);
        }

        (void) storage_.put(".unique_id", out);
    }
}
