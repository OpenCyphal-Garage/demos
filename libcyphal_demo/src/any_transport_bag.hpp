// This software is distributed under the terms of the MIT License.
// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
// Copyright Amazon.com Inc. or its affiliates.
// SPDX-License-Identifier: MIT
// Author: Sergei Shirokov <sergei.shirokov@zubax.com>

#ifndef ANY_TRANSPORT_BAG_HPP_INCLUDED
#define ANY_TRANSPORT_BAG_HPP_INCLUDED

#include <libcyphal/transport/transport.hpp>
#include <libcyphal/types.hpp>

/// Represents storage of some (UDP, CAN) libcyphal transport and its media.
///
class AnyTransportBag
{
public:
    using Ptr       = libcyphal::UniquePtr<AnyTransportBag>;
    using Transport = libcyphal::transport::ITransport;

    AnyTransportBag(const AnyTransportBag&)                = delete;
    AnyTransportBag(AnyTransportBag&&) noexcept            = delete;
    AnyTransportBag& operator=(const AnyTransportBag&)     = delete;
    AnyTransportBag& operator=(AnyTransportBag&&) noexcept = delete;

    virtual Transport& getTransport() const = 0;

protected:
    AnyTransportBag()  = default;
    ~AnyTransportBag() = default;

};  // AnyTransportBag

#endif  // ANY_TRANSPORT_BAG_HPP_INCLUDED
