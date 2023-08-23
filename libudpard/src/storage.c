/// This software is distributed under the terms of the MIT License.
/// Copyright (C) 2021 OpenCyphal <maintainers@opencyphal.org>
/// Author: Pavel Kirienko <pavel@opencyphal.org>

#include "storage.h"
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

/// We could use key names directly, but they may be long, which leads to unnecessary storage memory consumption;
/// additionally, certain storage implementations may not be able to store arbitrary-length keys.
/// Therefore, we use a hash of the key. It makes key listing impossible but it is not required by the application.
///
/// The hash length can be adjusted as necessary to trade-off collision probability vs. space requirement.
/// If the underlying hash function is CRC64 and the radix is 62, the maximum sensible hash size is 11 digits.
/// A 7-digit hash with radix 62 is representable in a ceil(log2(62**7)) = 43-bit integer.
/// A 6-digit hash with radix 62 is representable in a ceil(log2(62**6)) = 36-bit integer.
/// The collision probability for a 6-digit base-62 hash with 200 keys is:
///
///     >>> n=200
///     >>> d=Decimal(62**6)
///     >>> 1- ((d-1)/d) ** ((n*(n-1))//2)
///     3.5e-7
///     >>> _*100
///     0.000035
#define KEY_HASH_LENGTH 6U
struct KeyHash
{
    char hash[KEY_HASH_LENGTH + 1];  // Digits plus the null terminator.
};

static uint64_t computeKeyHashValue(const char* const key)
{
    // In this implementation we use CRC-64/WE: http://reveng.sourceforge.net/crc-catalogue/17plus.htm#crc.cat-bits.64
    static const uint64_t Poly       = 0x42F0E1EBA9EA3693ULL;
    static const uint64_t Mask       = 1ULL << 63U;
    static const uint64_t InputShift = 56U;
    static const uint64_t OctetWidth = 8U;
    uint64_t              hash       = UINT64_MAX;
    const unsigned char*  ptr        = (const unsigned char*) key;
    while (*ptr != 0)
    {
        hash ^= (uint64_t) (*ptr) << InputShift;
        ++ptr;
        for (uint_fast8_t i = 0; i < OctetWidth; i++)
        {
            hash = ((hash & Mask) != 0) ? ((hash << 1U) ^ Poly) : (hash << 1U);
        }
    }
    return hash;
}

static struct KeyHash computeKeyHash(const char* const key)
{
    static const char     Alphabet[] = "0123456789"
                                       "abcdefghijklmnopqrstuvwxyz"
                                       "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static const uint64_t Radix      = sizeof(Alphabet) - 1U;
    uint64_t              value      = computeKeyHashValue(key);
    size_t                index      = 0;
    struct KeyHash        out        = {.hash = {0}};  // The hash contains the digits in reverse order.
    do
    {
        out.hash[index] = Alphabet[value % Radix];
        value /= Radix;
        index++;
    } while ((value > 0) && (index < KEY_HASH_LENGTH));
    return out;
}

static FILE* keyOpen(const char* const key, const bool write)
{
    return fopen(&computeKeyHash(key).hash[0], write ? "wb" : "rb");
}

bool storageGet(const char* const key, const size_t size, void* const data)
{
    bool result = false;
    if ((key != NULL) && (data != NULL))
    {
        FILE* const fp = keyOpen(key, false);
        if (fp != NULL)
        {
            result = (fread(data, 1U, size, fp) == size) && (ferror(fp) == 0);
            (void) fclose(fp);
        }
    }
    return result;
}

bool storagePut(const char* const key, const size_t size, const void* const data)
{
    bool result = false;
    if ((key != NULL) && (data != NULL))
    {
        FILE* const fp = keyOpen(key, true);
        if (fp != NULL)
        {
            result = (fwrite(data, 1U, size, fp) == size) && (ferror(fp) == 0);
            (void) fclose(fp);
        }
    }
    return result;
}

bool storageDrop(const char* const key)
{
    return (key != NULL) && (unlink(&computeKeyHash(key).hash[0]) == 0);
}
