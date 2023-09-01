///                            ____                   ______            __          __
///                           / __ `____  ___  ____  / ____/_  ______  / /_  ____  / /
///                          / / / / __ `/ _ `/ __ `/ /   / / / / __ `/ __ `/ __ `/ /
///                         / /_/ / /_/ /  __/ / / / /___/ /_/ / /_/ / / / / /_/ / /
///                         `____/ .___/`___/_/ /_/`____/`__, / .___/_/ /_/`__,_/_/
///                             /_/                     /____/_/
///
/// In Cyphal, registers are named values that can be read and possibly written over the network. They are used
/// extensively to configure the application and to expose its states to the network. This module provides one
/// possible implementation of the Cyphal register API. Since the implementation of the register API does not
/// affect wire compatibility, the Cyphal Specification does not mandate any particular approach; as such,
/// other applications may approach the same problem differently.
///
/// This design is based on an AVL tree indexed by the CRC-64/WE hash of the register name. The tree is used
/// to quickly find the register by name in logarithmic time. The tree is also traversed in the index order to
/// implement the index-based access.
///
/// This software is distributed under the terms of the MIT License.
/// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
/// Copyright Amazon.com Inc. or its affiliates.
/// SPDX-License-Identifier: MIT
/// Author: Pavel Kirienko <pavel@opencyphal.org>

#pragma once

#include <cavl.h>
#include <uavcan/_register/Access_1_0.h>

/// Represents a local node register.
/// Cyphal registers are named values described in the uavcan.register.Access DSDL definition.
/// This type can also be used as a supertype to implement advanced capabilities.
struct Register
{
    Cavl base;  ///< Do not modify.

    /// The name is always null-terminated. The name hash is used for faster lookup to avoid string comparisons.
    char     name[uavcan_register_Name_1_0_name_ARRAY_CAPACITY_ + 1];
    uint64_t name_hash;

    /// The metadata fields are mostly useful when serving remote access requests.
    bool persistent;      ///< The value is stored in non-volatile memory. The application is responsible for that.
    bool remote_mutable;  ///< The value can be changed over the network.

    /// The type of the value shall not change after initialization (see the registry API specification).
    /// If accessor functions are used, the value of this field is ignored/undefined.
    uavcan_register_Value_1_0 value;
    /// If getter is non-NULL, the value will be obtained through this callback instead of the value field.
    uavcan_register_Value_1_0 (*getter)(struct Register*);

    /// An arbitrary user-defined pointer. This is mostly useful with the callbacks.
    void* user_reference;
};

/// Inserts the register into the tree. The caller must initialize the value/getter fields after this call.
/// Behavior undefined if the self or root or name fragment pointers are NULL.
/// The name fragment sequence is terminated by a NULL pointer; the fragments will be joined via "."; e.g.,
/// ("uavcan", "node", "id", NULL) --> "uavcan.node.id".
/// If such register already exists, it will be replaced.
///
/// The register is initialized as non-persistent and immutable; this can be changed afterward.
///
/// Addition of a new register invalidates the indexes; hence, in the interest of preserving the indexes,
/// the application should init all registers at once during startup and then never modify the tree.
void registerInit(struct Register* const  self,
                  struct Register** const root,
                  const char** const      null_terminated_name_fragments);

/// Copy one value to the other if their types and dimensionality are the same or an automatic conversion is possible.
/// If the destination is empty, it is simply replaced with the source (assignment always succeeds).
/// Assignment always fails if the source is empty, unless the destination is also empty.
/// The return value is true if the assignment has been performed, false if it is not possible;
/// in the latter case the destination is not modified.
///
/// This function is useful when accepting register write requests from the network.
bool registerAssign(uavcan_register_Value_1_0* const dst, const uavcan_register_Value_1_0* const src);

/// Traverse all registers in their index order, invoke the functor on each.
/// The user reference will be passed to the functor as-is.
/// Traversal will stop either when all registers are traversed or when the functor returns non-NULL.
/// Returns the pointer returned by the functor or NULL if all registers were traversed.
void* registerTraverse(struct Register* const root,
                       void* (*const fun)(struct Register*, void*),
                       void* const user_reference);

/// These search functions are needed to implement the Cyphal register API.
/// Returns NULL if not found or the arguments are invalid.
struct Register* registerFindByName(struct Register* const root, const char* const name);
struct Register* registerFindByIndex(struct Register* const root, const size_t index);
