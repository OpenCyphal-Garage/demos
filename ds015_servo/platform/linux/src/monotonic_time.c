/// This software is distributed under the terms of the MIT License.
/// Copyright (C) 2021 UAVCAN Consortium <consortium@uavcan.org>
/// Author: Pavel Kirienko <pavel@uavcan.org>

#include "monotonic_time.h"
#include <time.h>
#include <stdlib.h>

CanardMicrosecond getMonotonicMicroseconds()
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        abort();
    }
    return (uint64_t)(ts.tv_sec * 1000000 + ts.tv_nsec / 1000);
}
