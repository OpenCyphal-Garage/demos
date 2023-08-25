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
    bool persistent;      ///< The value is stored in non-volatile memory.
    bool remote_mutable;  ///< The value can be changed over the network.

    /// The type of the value shall not change after initialization (see the registry API specification).
    /// If accessor functions are used, the value of this field is ignored/undefined.
    uavcan_register_Value_1_0 value;
    /// If getter is non-NULL, the value will be obtained through this callback instead of the value field.
    uavcan_register_Value_1_0 (*getter)(struct Register*);
};

/// The port register sets are helpers to simplify the implementation of port configuration/introspection registers.
/// The value types are enforced by the implementation, so the application can rely on that.
struct PortRegisterSet
{
    struct Register id;    ///< uavcan.(pub|sub|cln|srv).PORT_NAME.id      : natural16[1]
    struct Register type;  ///< uavcan.(pub|sub|cln|srv).PORT_NAME.type    : string
};
struct PublisherRegisterSet
{
    struct PortRegisterSet base;
    struct Register        priority;  ///< uavcan.(pub|sub|cln|srv).PORT_NAME.prio    : natural8[1]
};
struct SubscriberRegisterSet
{
    struct PortRegisterSet base;
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

void registerInitPublisher(struct PublisherRegisterSet* const self,
                           struct Register** const            root,
                           const char* const                  port_name,
                           const char* const                  port_type);

void registerInitSubscriber(struct SubscriberRegisterSet* const self,
                            struct Register** const             root,
                            const char* const                   port_name,
                            const char* const                   port_type);

/// Traverse all registers in their index order, invoke the functor on each.
/// The user reference will be passed to the functor as-is.
/// Traversal will stop either when all registers are traversed or when the functor returns non-NULL.
/// Returns the pointer returned by the functor or NULL if all registers were traversed.
void* registerTraverse(struct Register* const root,
                       void* (*const fun)(struct Register*, void*),
                       void* const user_reference);

/// These search functions are needed to implement the Cyphal register API.
/// The name length is provided to simplify coupling with the DSDL-generated code.
/// Returns NULL if not found or the arguments are invalid.
struct Register* registerFindByName(struct Register* const root, const size_t name_length, const char* const name);
struct Register* registerFindByIndex(struct Register* const root, const size_t index);
