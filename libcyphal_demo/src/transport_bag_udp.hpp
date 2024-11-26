// This software is distributed under the terms of the MIT License.
// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
// Copyright Amazon.com Inc. or its affiliates.
// SPDX-License-Identifier: MIT
// Author: Sergei Shirokov <sergei.shirokov@zubax.com>

#ifndef TRANSPORT_BAG_UDP_HPP_INCLUDED
#define TRANSPORT_BAG_UDP_HPP_INCLUDED

#include "application.hpp"
#include "platform/block_memory_resource.hpp"
#include "platform/common_helpers.hpp"
#include "platform/posix/udp/udp_media.hpp"

#include <udpard.h>  // for `alignof(UdpardTxItem)`

#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/executor.hpp>
#include <libcyphal/transport/errors.hpp>
#include <libcyphal/transport/udp/udp_transport.hpp>
#include <libcyphal/transport/udp/udp_transport_impl.hpp>
#include <libcyphal/types.hpp>

#include <cstddef>
#include <iostream>
#include <utility>

/// Holds (internally) instance of the UDP transport and its media (if any).
///
struct TransportBagUdp final
{
    TransportBagUdp(cetl::pmr::memory_resource&    general_memory,
                    libcyphal::IExecutor&          executor,
                    platform::BlockMemoryResource& media_block_mr)
        : general_mr_{general_memory}
        , executor_{executor}
        , media_block_mr_{media_block_mr}
        , media_collection_{general_memory, executor, media_block_mr}
    {
    }

    libcyphal::transport::udp::IUdpTransport* create(const Application::IfaceParams& params)
    {
        if (params.udp_iface.value().empty())
        {
            return nullptr;
        }

        media_collection_.parse(params.udp_iface.value());
        auto maybe_udp_transport = makeTransport({general_mr_}, executor_, media_collection_.span(), TxQueueCapacity);
        if (const auto* failure = cetl::get_if<libcyphal::transport::FactoryFailure>(&maybe_udp_transport))
        {
            std::cerr << "❌ Failed to create UDP transport (iface='"
                      << static_cast<cetl::string_view>(params.udp_iface.value()) << "').\n";
            return nullptr;
        }
        transport_ = cetl::get<libcyphal::UniquePtr<libcyphal::transport::udp::IUdpTransport>>(  //
            std::move(maybe_udp_transport));

        std::cout << "UDP Iface : '" << params.udp_iface.value().c_str() << "'\n";
        const std::size_t mtu = transport_->getProtocolParams().mtu_bytes;
        std::cout << "Iface MTU : " << mtu << "\n";

        // Udpard still allocates memory for `TxItem`+payload, so there is alignment requirement.
        // TODO: Relax this alignment down to 1 when it will be used for raw bytes block allocations only.
        //       Delete also `#include <udpard.h>` above. Eliminate `mtu + sizeof(UdpardTxItem) + 8U` to be just `mtu`.
        constexpr std::size_t block_alignment = alignof(UdpardTxItem);
        const std::size_t     block_size      = mtu + sizeof(UdpardTxItem) + 8U;
        const std::size_t     pool_size       = media_collection_.count() * TxQueueCapacity * block_size;
        media_block_mr_.setup(pool_size, block_size, block_alignment);

        transport_->setTransientErrorHandler(platform::CommonHelpers::Udp::transientErrorReporter);

        return transport_.get();
    }

private:
    static constexpr std::size_t TxQueueCapacity = 16;

    cetl::pmr::memory_resource&                                    general_mr_;
    libcyphal::IExecutor&                                          executor_;
    platform::BlockMemoryResource&                                 media_block_mr_;
    platform::posix::UdpMediaCollection                            media_collection_;
    libcyphal::UniquePtr<libcyphal::transport::udp::IUdpTransport> transport_;

};  // TransportBagUdp

#endif  // TRANSPORT_BAG_UDP_HPP_INCLUDED
