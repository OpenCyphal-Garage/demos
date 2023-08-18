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
#define MEMORY_BLOCK_ALLOCATOR_DEFINE(_name, _block_size_bytes, _block_count)   \
    static uint_least8_t        _name##_pool[_block_size_bytes * _block_count]; \
    struct MemoryBlockAllocator _name = memoryBlockInit(sizeof(_name##_pool), &_name##_pool[0], _block_size_bytes)

struct MemoryBlockAllocator
{
    size_t allocated_blocks;  ///< Read-only diagnostic counter.
    size_t block_size_bytes;  ///< Do not change.
    void*  head;              ///< Do not change.
};

/// Constructs a memory block allocator bound to the specified memory pool.
/// The block count will be deduced from the pool size and block size; both may be adjusted to ensure alignment.
struct MemoryBlockAllocator memoryBlockInit(const size_t pool_size_bytes,
                                            void* const  pool,
                                            const size_t block_size_bytes)
{
    const size_t bs = (block_size_bytes + sizeof(max_align_t) - 1U) & ~(sizeof(max_align_t) - 1U);
    assert(((uintptr_t) pool % sizeof(max_align_t)) == 0);
    assert((pool_size_bytes % bs) == 0);
    const size_t block_count = pool_size_bytes / bs;
    for (size_t i = 0; i < block_count; i++)
    {
        *(void**) (void*) (((uint_least8_t*) pool) + (i * bs)) =
            ((i + 1) < block_count)  // Next block exists?
                ? ((void*) (((uint_least8_t*) pool) + ((i + 1) * bs)))
                : NULL;
    }
    const struct MemoryBlockAllocator out = {.allocated_blocks = 0, .block_size_bytes = bs, .head = pool};
    return out;
}

void* memoryBlockAllocate(void* const user_reference, const size_t size)
{
    void*                              out  = NULL;
    struct MemoryBlockAllocator* const self = (struct MemoryBlockAllocator*) user_reference;
    assert(self != NULL);
    if ((size > 0) && (size <= self->block_size_bytes))
    {
        out = self->head;
        if (self->head != NULL)
        {
            self->head = *(void**) self->head;
            self->allocated_blocks++;
        }
    }
    return out;
}

void memoryBlockDeallocate(void* const user_reference, const size_t size, void* const pointer)
{
    struct MemoryBlockAllocator* const self = (struct MemoryBlockAllocator*) user_reference;
    assert((self != NULL) && (size <= self->block_size_bytes));
    if (pointer != NULL)
    {
        *(void**) pointer = self->head;
        self->head        = pointer;
        self->allocated_blocks--;
    }
}
