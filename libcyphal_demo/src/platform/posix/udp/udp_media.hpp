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
#include <libcyphal/executor.hpp>
#include <libcyphal/transport/udp/media.hpp>
#include <libcyphal/transport/udp/tx_rx_sockets.hpp>

#include <algorithm>
#include <array>

namespace platform
{
namespace posix
{

class UdpMedia final : public libcyphal::transport::udp::IMedia
{
public:
    UdpMedia(cetl::pmr::memory_resource& memory, libcyphal::IExecutor& executor, const cetl::string_view iface_address)
        : memory_{memory}
        , executor_{executor}
        , iface_address_{iface_address}
    {
    }

    ~UdpMedia() = default;

    UdpMedia(const UdpMedia&)                = delete;
    UdpMedia& operator=(const UdpMedia&)     = delete;
    UdpMedia* operator=(UdpMedia&&) noexcept = delete;

    UdpMedia(UdpMedia&& other) noexcept
        : memory_{other.memory_}
        , executor_{other.executor_}
        , iface_address_{other.iface_address_}
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
        return UdpTxSocket::make(memory_, executor_, iface_address_.data());
    }

    MakeRxSocketResult::Type makeRxSocket(const libcyphal::transport::udp::IpEndpoint& multicast_endpoint) override
    {
        return UdpRxSocket::make(memory_, executor_, iface_address_.data(), multicast_endpoint);
    }

    cetl::pmr::memory_resource& getTxMemoryResource() override
    {
        return memory_;
    }

    // MARK: Data members:

    cetl::pmr::memory_resource& memory_;
    libcyphal::IExecutor&       executor_;
    String<64>                  iface_address_;

};  // UdpMedia

// MARK: -

struct UdpMediaCollection
{
    UdpMediaCollection(cetl::pmr::memory_resource& memory, libcyphal::IExecutor& executor)
        : media_array_{{{memory, executor, ""}, {memory, executor, ""}, {memory, executor, ""}}}
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

private:
    static constexpr std::size_t MaxUdpMedia = 3;

    std::array<UdpMedia, MaxUdpMedia>                           media_array_;
    std::array<libcyphal::transport::udp::IMedia*, MaxUdpMedia> media_ifaces_{};

};  // UdpMediaCollection

}  // namespace posix
}  // namespace platform

#endif  // PLATFORM_POSIX_UDP_MEDIA_HPP_INCLUDED
