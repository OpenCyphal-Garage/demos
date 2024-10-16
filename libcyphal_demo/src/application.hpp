// This software is distributed under the terms of the MIT License.
// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
// Copyright Amazon.com Inc. or its affiliates.
// SPDX-License-Identifier: MIT
// Author: Sergei Shirokov <sergei.shirokov@zubax.com>

#ifndef APPLICATION_HPP
#define APPLICATION_HPP

#include "platform/linux/epoll_single_threaded_executor.hpp"
#include "platform/o1_heap_memory_resource.hpp"

#include <cetl/pf17/cetlpf.hpp>

/// The main application class.
///
/// Expected to be a singleton.
///
class Application final
{
public:
    Application();
    ~Application();

    Application(const Application&)            = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&)                 = delete;
    Application& operator=(Application&&)      = delete;

    CETL_NODISCARD platform::Linux::EpollSingleThreadedExecutor& executor() noexcept
    {
        return executor_;
    }

    CETL_NODISCARD cetl::pmr::memory_resource& memory() noexcept
    {
        return o1_heap_mr_;
    }

private:
    // MARK: Data members:

    platform::Linux::EpollSingleThreadedExecutor executor_;
    platform::O1HeapMemoryResource               o1_heap_mr_;

};  // Application

#endif  // APPLICATION_HPP
