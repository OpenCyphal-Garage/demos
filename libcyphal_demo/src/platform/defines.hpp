// This software is distributed under the terms of the MIT License.
// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
// Copyright Amazon.com Inc. or its affiliates.
// SPDX-License-Identifier: MIT
// Author: Sergei Shirokov <sergei.shirokov@zubax.com>

#ifndef PLATFORM_DEFINES_HPP_INCLUDED
#define PLATFORM_DEFINES_HPP_INCLUDED

#ifdef PLATFORM_OS_TYPE_BSD
#    include "bsd/kqueue_single_threaded_executor.hpp"
#else
#    include "linux/epoll_single_threaded_executor.hpp"
#endif

namespace platform
{

#ifdef PLATFORM_OS_TYPE_BSD
using SingleThreadedExecutor = bsd::KqueueSingleThreadedExecutor;
#else
using SingleThreadedExecutor = Linux::EpollSingleThreadedExecutor;
#endif

}  // namespace platform

#endif  // PLATFORM_DEFINES_HPP_INCLUDED
