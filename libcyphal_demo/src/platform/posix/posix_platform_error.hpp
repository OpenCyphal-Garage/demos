// This software is distributed under the terms of the MIT License.
// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
// Copyright Amazon.com Inc. or its affiliates.
// SPDX-License-Identifier: MIT
// Author: Sergei Shirokov <sergei.shirokov@zubax.com>

#ifndef PLATFORM_POSIX_PLATFORM_ERROR_HPP_INCLUDED
#define PLATFORM_POSIX_PLATFORM_ERROR_HPP_INCLUDED

#include <cetl/cetl.hpp>
#include <libcyphal/transport/errors.hpp>

#include <cstdint>

namespace platform
{
namespace posix
{

class PosixPlatformError final : public libcyphal::transport::IPlatformError
{
public:
    explicit PosixPlatformError(const int err)
        : code_{err}
    {
        CETL_DEBUG_ASSERT(err > 0, "");
    }
    virtual ~PosixPlatformError()                                = default;
    PosixPlatformError(const PosixPlatformError&)                = default;
    PosixPlatformError(PosixPlatformError&&) noexcept            = default;
    PosixPlatformError& operator=(const PosixPlatformError&)     = default;
    PosixPlatformError& operator=(PosixPlatformError&&) noexcept = default;

    // MARK: IPlatformError

    /// Gets platform-specific error code.
    ///
    /// In this case, the error code is the POSIX error code (aka `errno`).
    ///
    std::uint32_t code() const noexcept override
    {
        return static_cast<std::uint32_t>(code_);
    }

private:
    int code_;

};  // PosixPlatformError

}  // namespace posix
}  // namespace platform

#endif  // PLATFORM_POSIX_PLATFORM_ERROR_HPP_INCLUDED
