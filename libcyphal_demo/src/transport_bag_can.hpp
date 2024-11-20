// This software is distributed under the terms of the MIT License.
// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
// Copyright Amazon.com Inc. or its affiliates.
// SPDX-License-Identifier: MIT
// Author: Sergei Shirokov <sergei.shirokov@zubax.com>

#ifndef TRANSPORT_BAG_CAN_HPP_INCLUDED
#define TRANSPORT_BAG_CAN_HPP_INCLUDED

#include "application.hpp"
#include "platform/common_helpers.hpp"
#include "platform/linux/can/can_media.hpp"

#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/transport/can/can_transport.hpp>
#include <libcyphal/transport/can/can_transport_impl.hpp>
#include <libcyphal/types.hpp>

#include <cstddef>

/// Holds (internally) instance of the CAN transport and its media (if any).
///
struct TransportBagCan final
{
    TransportBagCan(cetl::pmr::memory_resource& general_mr,
                    libcyphal::IExecutor&       executor,
                    cetl::pmr::memory_resource& block_mr)
        : general_mr_{general_mr}
        , executor_{executor}
        , media_collection_{general_mr, executor, block_mr}
    {
    }

    libcyphal::transport::can::ICanTransport* create(const Application::IfaceParams& params)
    {
        if (params.can_iface.value().empty())
        {
            return nullptr;
        }

        media_collection_.parse(params.can_iface.value());
        auto maybe_can_transport = makeTransport({general_mr_}, executor_, media_collection_.span(), TxQueueCapacity);
        if (const auto* failure = cetl::get_if<libcyphal::transport::FactoryFailure>(&maybe_can_transport))
        {
            std::cerr << "❌ Failed to create CAN transport (iface='"
                      << static_cast<cetl::string_view>(params.can_iface.value()) << "').\n";
            return nullptr;
        }
        transport_ = cetl::get<libcyphal::UniquePtr<libcyphal::transport::can::ICanTransport>>(  //
            std::move(maybe_can_transport));

        std::cout << "CAN Iface : '" << params.can_iface.value().c_str() << "'\n";

        transport_->setTransientErrorHandler(platform::CommonHelpers::Can::transientErrorReporter);

        return transport_.get();
    }

private:
    static constexpr std::size_t TxQueueCapacity = 16;

    cetl::pmr::memory_resource&                                    general_mr_;
    libcyphal::IExecutor&                                          executor_;
    platform::Linux::CanMediaCollection                            media_collection_;
    libcyphal::UniquePtr<libcyphal::transport::can::ICanTransport> transport_;

};  // TransportBagCan

#endif  // TRANSPORT_BAG_CAN_HPP_INCLUDED
