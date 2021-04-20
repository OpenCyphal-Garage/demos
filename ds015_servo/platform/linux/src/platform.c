/// This software is distributed under the terms of the MIT License.
/// Copyright (C) 2021 UAVCAN Consortium <consortium@uavcan.org>

#include "platform.h"


// A simple application like a servo node typically does not require more than 16 KiB of heap and 4 KiB of stack.
// For the background and related theory refer to the following resources:
// - https://github.com/UAVCAN/libcanard/blob/master/README.md
// - https://github.com/pavel-kirienko/o1heap/blob/master/README.md
// - https://forum.uavcan.org/t/uavcanv1-libcanard-nunavut-templates-memory-usage-concerns/1118/4?u=pavel.kirienko
_Alignas(O1HEAP_ALIGNMENT) static uint8_t heap_arena[1024 * 16] = {0};


int init_platform(O1HeapInstance** out_allocator)
{
    int result = 0;
    if (out_allocator)
    {
        // If you are using an RTOS or another multithreaded environment, pass critical section enter/leave functions
        // in the last two arguments instead of NULL.
        O1HeapInstance* inst = o1heapInit(heap_arena, sizeof(heap_arena), NULL, NULL);
        if (inst == NULL)
        {
            result = 1;
        }
        *out_allocator = inst;
    }
    return result;
}

void service(void)
{
    // Linux is boring. We don't do anything here.
}
