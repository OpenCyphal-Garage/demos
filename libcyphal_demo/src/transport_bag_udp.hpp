// This software is distributed under the terms of the MIT License.
// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
// Copyright Amazon.com Inc. or its affiliates.
// SPDX-License-Identifier: MIT
// Author: Sergei Shirokov <sergei.shirokov@zubax.com>

#ifndef TRANSPORT_BAG_UDP_HPP_INCLUDED
#define TRANSPORT_BAG_UDP_HPP_INCLUDED

#include "any_transport_bag.hpp"
#include "application.hpp"
#include "platform/block_memory_resource.hpp"
#include "platform/common_helpers.hpp"
#include "platform/posix/udp/udp_media.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>
#include <cetl/pmr/interface_ptr.hpp>
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
class TransportBagUdp final : public AnyTransportBag
{
    /// Defines private specification for making interface unique ptr.
    ///
    struct Spec : libcyphal::detail::UniquePtrSpec<AnyTransportBag, TransportBagUdp>
    {
        explicit Spec() = default;
    };

public:
    Transport& getTransport() const override
    {
        CETL_DEBUG_ASSERT(transport_, "");
        return *transport_;
    }

    static Ptr make(cetl::pmr::memory_resource&     general_mr,
                    libcyphal::IExecutor&           executor,
                    platform::BlockMemoryResource&  media_block_mr,
                    const Application::IfaceParams& params)

    {
        if (params.udp_iface.value().empty())
        {
            return nullptr;
        }

        auto any_transport_bag = cetl::pmr::InterfaceFactory::make_unique<AnyTransportBag>(  //
            cetl::pmr::polymorphic_allocator<TransportBagUdp>{&general_mr},
            Spec{},
            general_mr,
            executor,
            media_block_mr);
        if (!any_transport_bag)
        {
            return nullptr;
        }
        auto& bag = static_cast<TransportBagUdp&>(*any_transport_bag);  // NOLINT

        bag.media_collection_.parse(params.udp_iface.value());
        auto maybe_udp_transport = makeTransport({general_mr}, executor, bag.media_collection_.span(), TxQueueCapacity);
        if (const auto* failure = cetl::get_if<libcyphal::transport::FactoryFailure>(&maybe_udp_transport))
        {
            std::cerr << "❌ Failed to create UDP transport (iface='"
                      << static_cast<cetl::string_view>(params.udp_iface.value()) << "').\n";
            return nullptr;
        }
        bag.transport_ = cetl::get<libcyphal::UniquePtr<libcyphal::transport::udp::IUdpTransport>>(  //
            std::move(maybe_udp_transport));

        std::cout << "UDP Iface : '" << params.udp_iface.value().c_str() << "'\n";
        const std::size_t mtu = bag.transport_->getProtocolParams().mtu_bytes;
        std::cout << "Iface MTU : " << mtu << "\n";

        // Udpard allocates memory for raw bytes block only, so there is no alignment requirement.
        constexpr std::size_t block_alignment = 1;
        const std::size_t     block_size      = mtu;
        const std::size_t     pool_size       = bag.media_collection_.count() * TxQueueCapacity * block_size;
        bag.media_block_mr_.setup(pool_size, block_size, block_alignment);

        bag.transport_->setTransientErrorHandler(platform::CommonHelpers::Udp::transientErrorReporter);

        return any_transport_bag;
    }

    TransportBagUdp(Spec,
                    cetl::pmr::memory_resource&    general_memory,
                    libcyphal::IExecutor&          executor,
                    platform::BlockMemoryResource& media_block_mr)
        : general_mr_{general_memory}
        , executor_{executor}
        , media_block_mr_{media_block_mr}
        , media_collection_{general_memory, executor, media_block_mr}
    {
    }

private:
    using TransportPtr = libcyphal::UniquePtr<libcyphal::transport::udp::IUdpTransport>;

    static constexpr std::size_t TxQueueCapacity = 16;

    cetl::pmr::memory_resource&         general_mr_;
    libcyphal::IExecutor&               executor_;
    platform::BlockMemoryResource&      media_block_mr_;
    platform::posix::UdpMediaCollection media_collection_;
    TransportPtr                        transport_;

};  // TransportBagUdp

#endif  // TRANSPORT_BAG_UDP_HPP_INCLUDED
