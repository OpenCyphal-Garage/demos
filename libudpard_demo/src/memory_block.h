///                            ____                   ______            __          __
///                           / __ `____  ___  ____  / ____/_  ______  / /_  ____  / /
///                          / / / / __ `/ _ `/ __ `/ /   / / / / __ `/ __ `/ __ `/ /
///                         / /_/ / /_/ /  __/ / / / /___/ /_/ / /_/ / / / / /_/ / /
///                         `____/ .___/`___/_/ /_/`____/`__, / .___/_/ /_/`__,_/_/
///                             /_/                     /____/_/
///
/// A header-only implementation of a fairly standard O(1) free list block allocator, aka block pool allocator.
/// It is not required to use LibUDPard, as the library can be used with an ordinary malloc()/free() or
/// perhaps O1Heap (https://github.com/pavel-kirienko/o1heap) if the application requires a more sophisticated
/// memory management strategy.
///
/// This software is distributed under the terms of the MIT License.
/// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
/// Copyright Amazon.com Inc. or its affiliates.
/// SPDX-License-Identifier: MIT
/// Author: Pavel Kirienko <pavel@opencyphal.org>

#pragma once

#include <stdlib.h>
#include <assert.h>

/// This can be replaced with the standard malloc()/free(), if available.
/// This macro is a crude substitute for the missing metaprogramming facilities in C.
#define MEMORY_BLOCK_ALLOCATOR_DEFINE(_name, _block_size_bytes, _block_count)                      \
    _Alignas(max_align_t) static uint_least8_t _name##_pool[(_block_size_bytes) * (_block_count)]; \
    struct MemoryBlockAllocator _name = memoryBlockInit(sizeof(_name##_pool), &_name##_pool[0], (_block_size_bytes))

/// The user shall not attempt to change any of the fields manually.
struct MemoryBlockAllocator
{
    // Constant pool parameters.
    size_t block_count;
    size_t block_size_bytes;

    // Read-only diagnostic values.
    size_t   used_blocks;       ///< Blocks in use at the moment.
    size_t   used_blocks_peak;  ///< Maximum number of blocks used at any point in time.
    uint64_t request_count;     ///< Total number of allocation requests.
    uint64_t oom_count;         ///< Total number of out-of-memory errors.

    // Private fields.
    void* head;
};

/// Constructs a memory block allocator bound to the specified memory pool.
/// The block count will be deduced from the pool size and block size; both may be adjusted to ensure alignment.
/// If the pool or block size are not properly aligned, some memory may need to be wasted to enforce alignment.
static struct MemoryBlockAllocator memoryBlockInit(const size_t pool_size_bytes,
                                                   void* const  pool,
                                                   const size_t block_size_bytes)
{
    // Enforce alignment and padding of the input arguments. We may waste some space as a result.
    const size_t   bs       = (block_size_bytes + sizeof(max_align_t) - 1U) & ~(sizeof(max_align_t) - 1U);
    size_t         sz_bytes = pool_size_bytes;
    uint_least8_t* ptr      = (uint_least8_t*) pool;
    while ((((uintptr_t) ptr) % sizeof(max_align_t)) != 0)
    {
        ptr++;
        if (sz_bytes > 0)
        {
            sz_bytes--;
        }
    }
    const size_t block_count = sz_bytes / bs;
    for (size_t i = 0; i < block_count; i++)
    {
        *(void**) (void*) (ptr + (i * bs)) = ((i + 1) < block_count) ? ((void*) (ptr + ((i + 1) * bs))) : NULL;
    }
    const struct MemoryBlockAllocator out = {.block_count = block_count, .block_size_bytes = bs, .head = ptr};
    return out;
}

static void* memoryBlockAllocate(void* const user_reference, const size_t size)
{
    void*                              out  = NULL;
    struct MemoryBlockAllocator* const self = (struct MemoryBlockAllocator*) user_reference;
    assert(self != NULL);
    self->request_count++;
    if ((size > 0) && (size <= self->block_size_bytes))
    {
        out = self->head;
        if (self->head != NULL)
        {
            self->head = *(void**) self->head;
            self->used_blocks++;
            self->used_blocks_peak =
                (self->used_blocks > self->used_blocks_peak) ? self->used_blocks : self->used_blocks_peak;
        }
    }
    if (out == NULL)
    {
        self->oom_count++;
    }
    return out;
}

static void memoryBlockDeallocate(void* const user_reference, const size_t size, void* const pointer)
{
    struct MemoryBlockAllocator* const self = (struct MemoryBlockAllocator*) user_reference;
    assert((self != NULL) && (size <= self->block_size_bytes));
    (void) size;
    if (pointer != NULL)
    {
        *(void**) pointer = self->head;
        self->head        = pointer;
        assert(self->used_blocks > 0);
        self->used_blocks--;
    }
}
