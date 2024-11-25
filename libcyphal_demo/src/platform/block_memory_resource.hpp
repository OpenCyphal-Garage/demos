// This software is distributed under the terms of the MIT License.
// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
// Copyright Amazon.com Inc. or its affiliates.
// SPDX-License-Identifier: MIT
// Author: Sergei Shirokov <sergei.shirokov@zubax.com>

#ifndef PLATFORM_BLOCK_MEMORY_RESOURCE_HPP
#define PLATFORM_BLOCK_MEMORY_RESOURCE_HPP

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace platform
{

/// Implements a C++17 PMR memory resource that uses a pool of pre-allocated blocks.
///
template <std::size_t MaxAlign = sizeof(std::max_align_t)>
class BlockMemoryResource : public cetl::pmr::memory_resource
{
public:
    struct Diagnostics final
    {
        std::size_t   capacity;
        std::size_t   allocated;
        std::size_t   peak_allocated;
        std::size_t   block_size;
        std::uint64_t oom_count;

    };  // Diagnostics

    BlockMemoryResource()           = default;
    ~BlockMemoryResource() override = default;

    BlockMemoryResource(BlockMemoryResource&&)                 = delete;
    BlockMemoryResource(const BlockMemoryResource&)            = delete;
    BlockMemoryResource& operator=(BlockMemoryResource&&)      = delete;
    BlockMemoryResource& operator=(const BlockMemoryResource&) = delete;

    void setup(const std::size_t pool_size_bytes, void* const pool, const std::size_t block_size_bytes)
    {
        CETL_DEBUG_ASSERT(nullptr != pool, "");
        CETL_DEBUG_ASSERT(block_size_bytes > 0U, "");
        CETL_DEBUG_ASSERT(pool_size_bytes >= MaxAlign, "");

        // Enforce alignment and padding of the input arguments. We may waste some space as a result.
        const std::size_t bs       = (block_size_bytes + MaxAlign - 1U) & ~(MaxAlign - 1U);
        std::size_t       sz_bytes = pool_size_bytes;
        auto*             ptr      = static_cast<std::uint8_t*>(pool);
        while ((reinterpret_cast<std::uintptr_t>(ptr) % MaxAlign) != 0U)  // NOLINT
        {
            ptr++;  // NOLINT
            if (sz_bytes > 0U)
            {
                sz_bytes--;
            }
        }

        block_size_bytes_ = bs;
        block_count_      = sz_bytes / bs;
        head_             = reinterpret_cast<void**>(ptr);  // NOLINT

        for (std::size_t i = 0U; i < block_count_; i++)
        {
            *reinterpret_cast<void**>(ptr + (i * bs)) =                                           // NOLINT
                ((i + 1U) < block_count_) ? static_cast<void*>(ptr + ((i + 1U) * bs)) : nullptr;  // NOLINT
        }
    }

    Diagnostics queryDiagnostics() const noexcept
    {
        return {block_count_, used_blocks_, used_blocks_peak_, block_size_bytes_, oom_count_};
    }

protected:
    // MARK: cetl::pmr::memory_resource

    void* do_allocate(std::size_t size_bytes, std::size_t alignment) override  // NOLINT
    {
        if (alignment > MaxAlign)
        {
#if defined(__cpp_exceptions)
            throw std::bad_alloc();
#else
            return nullptr;
#endif
        }

        // C++ standard (basic.stc.dynamic.allocation) requires that a memory allocation never returns
        // nullptr (even for the zero).
        // So, we have to handle this case explicitly by returning a non-null pointer to an empty storage.
        if (size_bytes == 0U)
        {
            return empty_storage_.data();
        }

        void* out = nullptr;
        request_count_++;
        if (size_bytes <= block_size_bytes_)
        {
            out = static_cast<void*>(head_);  // NOLINT
            if (head_ != nullptr)
            {
                head_ = static_cast<void**>(*head_);  // NOLINT
                used_blocks_++;
                used_blocks_peak_ = std::max(used_blocks_, used_blocks_peak_);
            }
        }
        if (out == nullptr)
        {
            oom_count_++;
        }
        return out;
    }

    void do_deallocate(void* ptr, std::size_t size_bytes, std::size_t alignment) override  // NOLINT
    {
        CETL_DEBUG_ASSERT((nullptr != ptr) || (0U == size_bytes), "");
        CETL_DEBUG_ASSERT(size_bytes <= block_size_bytes_, "");
        (void) size_bytes;
        (void) alignment;

        // See `do_allocate` special case for zero bytes.
        if (ptr == empty_storage_.data())
        {
            CETL_DEBUG_ASSERT(0U == size_bytes, "");
            return;
        }

        if (ptr != nullptr)
        {
            *static_cast<void**>(ptr) = static_cast<void*>(head_);
            head_                     = static_cast<void**>(ptr);
            CETL_DEBUG_ASSERT(used_blocks_ > 0U, "");
            used_blocks_--;
        }
    }

    bool do_is_equal(const cetl::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

private:
    void**      head_{nullptr};
    std::size_t block_count_{0U};
    std::size_t block_size_bytes_{0U};
    std::size_t used_blocks_{0U};
    std::size_t used_blocks_peak_{0U};
    std::size_t request_count_{0U};
    std::size_t oom_count_{0U};

    // See `do_allocate` special case for zero bytes.
    // Note that we still need at least one byte - b/c `std::array<..., 0>::data()` returns `nullptr`.
    std::array<std::uint8_t, 1U> empty_storage_{};

};  // BlockMemoryResource

}  // namespace platform

#endif  // PLATFORM_BLOCK_MEMORY_RESOURCE_HPP