/// This software is distributed under the terms of the MIT License.
/// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
/// Copyright Amazon.com Inc. or its affiliates.
/// SPDX-License-Identifier: MIT
/// Author: Pavel Kirienko <pavel@opencyphal.org>

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/// CRC-64/WE, see http://reveng.sourceforge.net/crc-catalogue/17plus.htm#crc.cat-bits.64
static inline uint64_t crc64we(const size_t size, const void* const data)
{
    static const uint64_t Poly       = 0x42F0E1EBA9EA3693ULL;
    static const uint64_t Mask       = 1ULL << 63U;
    static const uint64_t InputShift = 56U;
    static const uint64_t OctetWidth = 8U;
    uint64_t              hash       = UINT64_MAX;
    for (size_t i = 0; i < size; i++)
    {
        hash ^= (uint64_t) (*(i + (const unsigned char*) data)) << InputShift;
        for (uint_fast8_t j = 0; j < OctetWidth; j++)
        {
            hash = ((hash & Mask) != 0) ? ((hash << 1U) ^ Poly) : (hash << 1U);
        }
    }
    return hash ^ UINT64_MAX;
}

static inline uint64_t crc64weString(const char* const str)
{
    return crc64we(strlen(str), str);
}
