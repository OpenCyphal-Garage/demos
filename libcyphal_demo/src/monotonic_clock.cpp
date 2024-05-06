/// This software is distributed under the terms of the MIT License.
/// Copyright (C) 2021 OpenCyphal <consortium@opencyphal.org>
/// Author: Sergei Shirokov <sergei.shirokov@zubax.com>

#include <libcyphal/types.hpp>

#include <chrono>

libcyphal::TimePoint libcyphal::MonotonicClock::now() noexcept
{
    const auto now = std::chrono::steady_clock::now();
    return libcyphal::TimePoint{std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch())};
}
