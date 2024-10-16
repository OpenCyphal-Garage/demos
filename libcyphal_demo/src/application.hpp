// This software is distributed under the terms of the MIT License.
// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
// Copyright Amazon.com Inc. or its affiliates.
// SPDX-License-Identifier: MIT
// Author: Sergei Shirokov <sergei.shirokov@zubax.com>

#ifndef APPLICATION_HPP
#define APPLICATION_HPP

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
    ~Application() = default;

    Application(const Application&)            = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&)                 = delete;
    Application& operator=(Application&&)      = delete;

    CETL_NODISCARD cetl::pmr::memory_resource& memory() noexcept
    {
        return memory_;
    }

private:
    // MARK: Data members:

    platform::O1HeapMemoryResource memory_;

};  // Application

#endif  // APPLICATION_HPP
