/// This software is distributed under the terms of the MIT License.
/// Copyright (C) 2021 OpenCyphal <consortium@opencyphal.org>
/// Author: Sergei Shirokov <sergei.shirokov@zubax.com>

#include "socket_can_media.hpp"

#include <canard.h>
#include <cetl/pf17/attribute.hpp>
#include <cetl/pf17/cetlpf.hpp>
#include <cetl/pf20/cetlpf.hpp>
#include <libcyphal/transport/can/media.hpp>
#include <libcyphal/transport/errors.hpp>
#include <libcyphal/types.hpp>
#include <socketcan.h>

#include <cstddef>
#include <memory>
#include <string>
#include <unistd.h>
#include <utility>

namespace can
{

std::unique_ptr<SocketCanMedia> SocketCanMedia::make(const std::string& iface_name, const bool is_fd)
{
    return std::make_unique<SocketCanMedia>(iface_name, is_fd);
}

SocketCanMedia::SocketCanMedia(std::string iface_name, const bool is_fd) :
    is_fd_{is_fd}, iface_name_{std::move(iface_name)}, socket_can_file_desc_{-1}
{
    socket_can_file_desc_ = ::socketcanOpen(iface_name_.c_str(), false);
}

SocketCanMedia::~SocketCanMedia()
{
    ::close(socket_can_file_desc_);
}

std::size_t SocketCanMedia::getMtu() const noexcept
{
    return is_fd_ ? CANARD_MTU_CAN_FD : CANARD_MTU_CAN_CLASSIC;
}

CETL_NODISCARD cetl::optional<libcyphal::transport::MediaError> SocketCanMedia::setFilters(
    const libcyphal::transport::can::Filters) noexcept
{
    return cetl::nullopt;
}

CETL_NODISCARD libcyphal::Expected<bool, libcyphal::transport::MediaError> SocketCanMedia::push(
    const libcyphal::TimePoint,
    const libcyphal::transport::can::CanId,
    const cetl::span<const cetl::byte>) noexcept
{
    return false;
}

CETL_NODISCARD libcyphal::Expected<cetl::optional<libcyphal::transport::can::RxMetadata>,
                                   libcyphal::transport::MediaError>
               SocketCanMedia::pop(const cetl::span<cetl::byte>) noexcept
{
    return cetl::nullopt;
}

}  // namespace can
