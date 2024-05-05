/// This software is distributed under the terms of the MIT License.
/// Copyright (C) 2021 OpenCyphal <consortium@opencyphal.org>
/// Author: Sergei Shirokov <sergei.shirokov@zubax.com>

#ifndef LIBCYPHAL_DEMO_SOCKET_CAN_MEDIA_HPP
#define LIBCYPHAL_DEMO_SOCKET_CAN_MEDIA_HPP

#include <cetl/pf17/attribute.hpp>
#include <cetl/pf17/cetlpf.hpp>
#include <cetl/pf20/cetlpf.hpp>
#include <libcyphal/transport/can/media.hpp>
#include <libcyphal/transport/errors.hpp>
#include <libcyphal/types.hpp>

#include <cstddef>
#include <memory>
#include <string>

namespace can
{

class SocketCanMedia final : public libcyphal::transport::can::IMedia
{
public:
    static std::unique_ptr<SocketCanMedia> make(const std::string& iface_name, const bool is_fd);

    SocketCanMedia(std::string iface_name, const bool is_fd);
    ~SocketCanMedia();

    SocketCanMedia(const SocketCanMedia&)                = delete;
    SocketCanMedia(SocketCanMedia&&) noexcept            = delete;
    SocketCanMedia& operator=(const SocketCanMedia&)     = delete;
    SocketCanMedia& operator=(SocketCanMedia&&) noexcept = delete;

private:
    // MARK: libcyphal::transport::can::IMedia

    std::size_t getMtu() const noexcept final;

    CETL_NODISCARD cetl::optional<libcyphal::transport::MediaError> setFilters(
        const libcyphal::transport::can::Filters filters) noexcept final;

    CETL_NODISCARD libcyphal::Expected<bool, libcyphal::transport::MediaError> push(
        const libcyphal::TimePoint             deadline,
        const libcyphal::transport::can::CanId can_id,
        const cetl::span<const cetl::byte>     payload) noexcept final;

    CETL_NODISCARD libcyphal::Expected<cetl::optional<libcyphal::transport::can::RxMetadata>,
                                       libcyphal::transport::MediaError>
                   pop(const cetl::span<cetl::byte> payload_buffer) noexcept final;

    // MARK: Data members:

    const bool        is_fd_;
    int               socket_can_file_desc_;
    const std::string iface_name_;

};  // SocketCanMedia

}  // namespace can

#endif  // LIBCYPHAL_DEMO_SOCKET_CAN_MEDIA_HPP
