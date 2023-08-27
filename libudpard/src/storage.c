/// This software is distributed under the terms of the MIT License.
/// Copyright (C) 2021 OpenCyphal <maintainers@opencyphal.org>
/// Author: Pavel Kirienko <pavel@opencyphal.org>

#include "storage.h"
#include "crc64we.h"
#include <stdio.h>
#include <string.h>
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
#define KEY_HASH_LENGTH 7U
struct KeyHash
{
    char hash[KEY_HASH_LENGTH + 1];  // Digits plus the null terminator.
};

static struct KeyHash computeKeyHash(const char* const key)
{
    static const char     Alphabet[] = "0123456789"
                                       "abcdefghijklmnopqrstuvwxyz"
                                       "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static const uint64_t Radix      = sizeof(Alphabet) - 1U;
    uint64_t              value      = crc64weString(key);
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
    char path[KEY_HASH_LENGTH + 4 + 1];  // Hash + extension + null terminator.
    (void) memcpy(&path[0], &computeKeyHash(key).hash[0], KEY_HASH_LENGTH);
    (void) memcpy(&path[KEY_HASH_LENGTH], ".cfg", 4);
    path[sizeof(path) - 1] = '\0';
    return fopen(&path[0], write ? "wb" : "rb");
}

bool storageGet(const char* const key, size_t* const inout_size, void* const data)
{
    bool result = false;
    if ((key != NULL) && (inout_size != NULL) && (data != NULL))
    {
        FILE* const fp = keyOpen(key, false);
        if (fp != NULL)
        {
            *inout_size = fread(data, 1U, *inout_size, fp);
            result      = (ferror(fp) == 0);
            (void) fclose(fp);
        }
    }
#if 0
    (void) fprintf(stderr,
                   "storageGet(%s, %zu, %p) -> %s\n",
                   key,
                   (inout_size == NULL) ? 0 : (*inout_size),
                   data,
                   result ? "YES" : "NO");
#endif
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
#if 0
    (void) fprintf(stderr, "storagePut(%s, %zu, %p) -> %s\n", key, size, data, result ? "YES" : "NO");
#endif
    return result;
}

bool storageDrop(const char* const key)
{
    return (key != NULL) && (unlink(&computeKeyHash(key).hash[0]) == 0);
}
