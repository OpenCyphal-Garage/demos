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
#include <iostream>

namespace
{

constexpr std::size_t HeapSize = 16ULL * 1024ULL;
alignas(O1HEAP_ALIGNMENT) std::array<cetl::byte, HeapSize> s_heap_arena{};

}  // namespace

Application::Application()
    : o1_heap_mr_{s_heap_arena}
{
}

Application::~Application()
{
    const auto mr_diag = o1_heap_mr_.queryDiagnostics();
    std::cout << "O(1) Heap diagnostics:" << "\n"
              << "  tcapacity=" << mr_diag.capacity << "\n"
              << "  tallocated=" << mr_diag.allocated << "\n"
              << "  tpeak_allocated=" << mr_diag.peak_allocated << "\n"
              << "  tpeak_request_size=" << mr_diag.peak_request_size << "\n"
              << "  toom_count=" << mr_diag.oom_count << "\n";
}