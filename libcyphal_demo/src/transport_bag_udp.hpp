// This software is distributed under the terms of the MIT License.
// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
// Copyright Amazon.com Inc. or its affiliates.
// SPDX-License-Identifier: MIT
// Author: Sergei Shirokov <sergei.shirokov@zubax.com>

#ifndef TRANSPORT_BAG_UDP_HPP_INCLUDED
#define TRANSPORT_BAG_UDP_HPP_INCLUDED

#include "application.hpp"
#include "platform/common_helpers.hpp"
#include "platform/posix/udp/udp_media.hpp"

#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/transport/udp/udp_transport.hpp>
#include <libcyphal/transport/udp/udp_transport_impl.hpp>
#include <libcyphal/types.hpp>

#include <cstddef>

/// Holds (internally) instance of the UDP transport and its media (if any).
///
struct TransportBagUdp final
{
    TransportBagUdp(cetl::pmr::memory_resource& memory, libcyphal::IExecutor& executor)
        : memory_{memory}
        , executor_{executor}
        , media_collection_{memory, executor}
    {
    }

    libcyphal::transport::udp::IUdpTransport* create(const Application::IfaceParams& params)
    {
        if (params.udp_iface.value().empty())
        {
            return nullptr;
        }

        media_collection_.parse(params.udp_iface.value());
        auto maybe_udp_transport = makeTransport({memory_}, executor_, media_collection_.span(), TxQueueCapacity);
        if (const auto* failure = cetl::get_if<libcyphal::transport::FactoryFailure>(&maybe_udp_transport))
        {
            std::cerr << "❌ Failed to create UDP transport (iface='"
                      << static_cast<cetl::string_view>(params.udp_iface.value()) << "').\n";
            return nullptr;
        }
        transport_ = cetl::get<libcyphal::UniquePtr<libcyphal::transport::udp::IUdpTransport>>(  //
            std::move(maybe_udp_transport));

        std::cout << "UDP Iface : '" << params.udp_iface.value().c_str() << "'\n";

        transport_->setTransientErrorHandler(platform::CommonHelpers::Udp::transientErrorReporter);

        return transport_.get();
    }

private:
    static constexpr std::size_t TxQueueCapacity = 16;

    cetl::pmr::memory_resource&                                    memory_;
    libcyphal::IExecutor&                                          executor_;
    platform::posix::UdpMediaCollection                            media_collection_;
    libcyphal::UniquePtr<libcyphal::transport::udp::IUdpTransport> transport_;

};  // TransportBagUdp

#endif  // TRANSPORT_BAG_UDP_HPP_INCLUDED
