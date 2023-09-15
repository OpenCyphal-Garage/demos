/// This software is distributed under the terms of the MIT License.
/// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
/// Copyright Amazon.com Inc. or its affiliates.
/// SPDX-License-Identifier: MIT
/// Author: Pavel Kirienko <pavel@opencyphal.org>

#include "register.h"
#include "crc64we.h"
#include <assert.h>

#define ASSIGN_SAME_TYPE(dst, src, type)                                                                    \
    do                                                                                                      \
    {                                                                                                       \
        if (uavcan_register_Value_1_0_is_##type##_(dst) && uavcan_register_Value_1_0_is_##type##_(src))     \
        {                                                                                                   \
            for (size_t i = 0; i < nunavutChooseMin((dst)->type.value.count, (src)->type.value.count); ++i) \
            {                                                                                               \
                (dst)->type.value.elements[i] = (src)->type.value.elements[i];                              \
            }                                                                                               \
            return true;                                                                                    \
        }                                                                                                   \
    } while (0)

static int_fast8_t treeSearchHash(void* const user_reference, const struct Cavl* const node)
{
    const uint64_t lhs = *(const uint64_t*) user_reference;
    const uint64_t rhs = ((const struct Register*) node)->name_hash;
    return (int_fast8_t) ((lhs < rhs) ? -1 : (lhs > rhs) ? 1 : 0);
}
static int_fast8_t treeSearchReg(void* const user_reference, const struct Cavl* const node)
{
    return treeSearchHash(&((struct Register*) user_reference)->name_hash, node);
}
static Cavl* treeFactory(void* const user_reference)
{
    return (Cavl*) user_reference;
}

void registerInit(struct Register* const  self,
                  struct Register** const root,
                  const char** const      null_terminated_name_fragments)
{
    assert((self != NULL) && (root != NULL) && (null_terminated_name_fragments != NULL));
    (void) memset(self, 0, sizeof(*self));
    {
        const char** nf        = null_terminated_name_fragments;
        char*        wp        = &self->name[0];
        size_t       remaining = sizeof(self->name);
        while (*nf != NULL)
        {
            if ((nf != null_terminated_name_fragments) && (remaining > 0))
            {
                *wp++ = '.';
                remaining--;
            }
            const size_t frag_len  = strlen(*nf);
            const size_t copy_size = (frag_len < remaining) ? frag_len : remaining;
            (void) memcpy(wp, *nf, copy_size);
            wp += copy_size;
            remaining -= copy_size;
            nf++;
        }
        self->name[uavcan_register_Name_1_0_name_ARRAY_CAPACITY_] = '\0';
    }
    self->name_hash = crc64weString(self->name);
    self->getter    = NULL;
    // Insert the register into the tree. Remove the old one if it exists.
    cavlRemove((Cavl**) root, cavlSearch((Cavl**) root, self, &treeSearchReg, NULL));
    const Cavl* const res = cavlSearch((Cavl**) root, self, &treeSearchReg, &treeFactory);
    assert(res == &self->base);
}

bool registerAssign(uavcan_register_Value_1_0* const dst, const uavcan_register_Value_1_0* const src)
{
    if (uavcan_register_Value_1_0_is_empty_(dst))
    {
        *dst = *src;
        return true;
    }
    if ((uavcan_register_Value_1_0_is_string_(dst) && uavcan_register_Value_1_0_is_string_(src)) ||
        (uavcan_register_Value_1_0_is_unstructured_(dst) && uavcan_register_Value_1_0_is_unstructured_(src)))
    {
        *dst = *src;
        return true;
    }
    if (uavcan_register_Value_1_0_is_bit_(dst) && uavcan_register_Value_1_0_is_bit_(src))
    {
        nunavutCopyBits(dst->bit.value.bitpacked,
                        0,
                        nunavutChooseMin(dst->bit.value.count, src->bit.value.count),
                        src->bit.value.bitpacked,
                        0);
        return true;
    }
    ASSIGN_SAME_TYPE(dst, src, integer64);
    ASSIGN_SAME_TYPE(dst, src, integer32);
    ASSIGN_SAME_TYPE(dst, src, integer16);
    ASSIGN_SAME_TYPE(dst, src, integer8);
    ASSIGN_SAME_TYPE(dst, src, natural64);
    ASSIGN_SAME_TYPE(dst, src, natural32);
    ASSIGN_SAME_TYPE(dst, src, natural16);
    ASSIGN_SAME_TYPE(dst, src, natural8);
    ASSIGN_SAME_TYPE(dst, src, real64);
    ASSIGN_SAME_TYPE(dst, src, real32);
    ASSIGN_SAME_TYPE(dst, src, real16);
    return false;
}

// NOLINTNEXTLINE(*-no-recursion)
void* registerTraverse(struct Register* const root,
                       void* (*const fun)(struct Register*, void*),
                       void* const user_reference)
{
    void* out = NULL;
    if (root != NULL)
    {
        out = registerTraverse((struct Register*) root->base.lr[0], fun, user_reference);
        if (out == NULL)
        {
            out = fun(root, user_reference);
        }
        if (out == NULL)
        {
            out = registerTraverse((struct Register*) root->base.lr[1], fun, user_reference);
        }
    }
    return out;
}

struct Register* registerFindByName(struct Register* const root, const char* const name)
{
    uint64_t name_hash = crc64weString(name);
    return (struct Register*) cavlSearch((Cavl**) &root, &name_hash, &treeSearchHash, NULL);
}

static void* indexTraverseFun(struct Register* const self, void* const user_reference)
{
    assert(user_reference != NULL);
    return ((*(size_t*) user_reference)-- == 0) ? self : NULL;
}
struct Register* registerFindByIndex(struct Register* const root, const size_t index)
{
    size_t i = index;
    return (struct Register*) registerTraverse(root, &indexTraverseFun, &i);
}
