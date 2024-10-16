// This software is distributed under the terms of the MIT License.
// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
// Copyright Amazon.com Inc. or its affiliates.
// SPDX-License-Identifier: MIT
// Author: Sergei Shirokov <sergei.shirokov@zubax.com>

#include "application.hpp"

#include <cetl/pf17/cetlpf.hpp>
#include <o1heap.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{

constexpr std::size_t HeapSize = 16ULL * 1024ULL;
alignas(O1HEAP_ALIGNMENT) std::array<cetl::byte, HeapSize> s_heap_arena{};

}  // namespace

Application::Application()
    : memory_{s_heap_arena}
{
}
