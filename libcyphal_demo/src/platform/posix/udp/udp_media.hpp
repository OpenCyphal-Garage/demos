// This software is distributed under the terms of the MIT License.
// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
// Copyright Amazon.com Inc. or its affiliates.
// SPDX-License-Identifier: MIT
// Author: Sergei Shirokov <sergei.shirokov@zubax.com>

#ifndef PLATFORM_POSIX_UDP_MEDIA_HPP_INCLUDED
#define PLATFORM_POSIX_UDP_MEDIA_HPP_INCLUDED

#include "platform/string.hpp"
#include "udp_sockets.hpp"

#include <cetl/pf17/cetlpf.hpp>
#include <cetl/pf20/cetlpf.hpp>
#include <libcyphal/executor.hpp>
#include <libcyphal/transport/udp/media.hpp>
#include <libcyphal/transport/udp/tx_rx_sockets.hpp>

#include <algorithm>
#include <array>
#include <cstddef>

namespace platform
{
namespace posix
{

class UdpMedia final : public libcyphal::transport::udp::IMedia
{
public:
    UdpMedia(cetl::pmr::memory_resource& general_mr,
             libcyphal::IExecutor&       executor,
             const cetl::string_view     iface_address,
             cetl::pmr::memory_resource& tx_mr)
        : general_mr_{general_mr}
        , executor_{executor}
        , iface_address_{iface_address}
        , tx_mr_{tx_mr}
    {
    }

    ~UdpMedia() = default;

    UdpMedia(const UdpMedia&)                = delete;
    UdpMedia& operator=(const UdpMedia&)     = delete;
    UdpMedia* operator=(UdpMedia&&) noexcept = delete;

    UdpMedia(UdpMedia&& other) noexcept
        : general_mr_{other.general_mr_}
        , executor_{other.executor_}
        , iface_address_{other.iface_address_}
        , tx_mr_{other.tx_mr_}
    {
    }

    void setAddress(const cetl::string_view iface_address)
    {
        iface_address_ = iface_address;
    }

private:
    // MARK: - IMedia

    MakeTxSocketResult::Type makeTxSocket() override
    {
        return UdpTxSocket::make(general_mr_, executor_, iface_address_.data());
    }

    MakeRxSocketResult::Type makeRxSocket(const libcyphal::transport::udp::IpEndpoint& multicast_endpoint) override
    {
        return UdpRxSocket::make(general_mr_, executor_, iface_address_.data(), multicast_endpoint);
    }

    cetl::pmr::memory_resource& getTxMemoryResource() override
    {
        return tx_mr_;
    }

    // MARK: Data members:

    cetl::pmr::memory_resource& general_mr_;
    libcyphal::IExecutor&       executor_;
    String<64>                  iface_address_;
    cetl::pmr::memory_resource& tx_mr_;

};  // UdpMedia

// MARK: -

struct UdpMediaCollection
{
    UdpMediaCollection(cetl::pmr::memory_resource& general_mr,
                       libcyphal::IExecutor&       executor,
                       cetl::pmr::memory_resource& tx_mr)
        : media_array_{{//
                        {general_mr, executor, "", tx_mr},
                        {general_mr, executor, "", tx_mr},
                        {general_mr, executor, "", tx_mr}}}
    {
    }

    void parse(const cetl::string_view iface_addresses)
    {
        // Split addresses by spaces.
        //
        std::size_t index = 0;
        std::size_t curr  = 0;
        while ((curr != cetl::string_view::npos) && (index < MaxUdpMedia))
        {
            const auto next          = iface_addresses.find(' ', curr);
            const auto iface_address = iface_addresses.substr(curr, next - curr);
            if (!iface_address.empty())
            {
                media_array_[index].setAddress(iface_address);  // NOLINT
                index++;
            }

            curr = std::max(next + 1, next);  // `+1` to skip the space
        }

        media_ifaces_ = {};
        for (std::size_t i = 0; i < index; i++)
        {
            media_ifaces_[i] = &media_array_[i];  // NOLINT
        }
    }

    cetl::span<libcyphal::transport::udp::IMedia*> span()
    {
        return {media_ifaces_.data(), media_ifaces_.size()};
    }

    std::size_t count() const
    {
        return std::count_if(media_ifaces_.cbegin(), media_ifaces_.cend(), [](const auto* iface) {
            //
            return iface != nullptr;
        });
    }

private:
    static constexpr std::size_t MaxUdpMedia = 3;

    std::array<UdpMedia, MaxUdpMedia>                           media_array_;
    std::array<libcyphal::transport::udp::IMedia*, MaxUdpMedia> media_ifaces_{};

};  // UdpMediaCollection

}  // namespace posix
}  // namespace platform

#endif  // PLATFORM_POSIX_UDP_MEDIA_HPP_INCLUDED
