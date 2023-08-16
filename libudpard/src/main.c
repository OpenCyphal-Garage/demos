/// This software is distributed under the terms of the MIT License.
/// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
/// Copyright Amazon.com Inc. or its affiliates.
/// SPDX-License-Identifier: MIT
/// Author: Pavel Kirienko <pavel@opencyphal.org>
///
/// This is a simple demo application that shows how to use LibUDPard. It is designed to be easily portable to any
/// baremetal embedded system; the Berkeley socket API will need to be replaced with whatever low-level UDP/IP stack
/// is used on the target platform. The demo application uses fixed-size block pools for dynamic memory management,
/// which is a common approach in deeply embedded systems. Applications where the ordinary heap is available can use
/// the standard malloc() and free() functions instead; or, if a hard real-time heap is needed, O1Heap may be used
/// instead: https://github.com/pavel-kirienko/o1heap.
///
/// The application performs dynamic node-ID allocation, subscribes to a subject and publishes to another subject.
/// Aside from that, it also publishes on the standard Heartbeat subject and responds to certain standard RPC requests.

#include <udpard.h>

/// The number of network interfaces used. LibUDPard natively supports non-redundant interfaces,
/// doubly-redundant, and triply-redundant network interfaces for fault tolerance.
#define IFACE_COUNT 2

typedef uint_least8_t byte_t;

struct MemoryBlockAllocator
{
    size_t  block_size_bytes;
    size_t  capacity_blocks;
    size_t  allocated_blocks;
    byte_t* head;
};

void* memoryBlockAllocate(void* const user_reference, const size_t size)
{
    //
}

void memoryBlockDeallocate(void* const user_reference, const size_t size, void* const pointer)
{
    //
}

struct Publisher
{
    UdpardPortID        subject_id;
    enum UdpardPriority priority;
    UdpardMicrosecond   tx_timeout;
    UdpardTransferID    transfer_id;
};

struct Subscriber
{
    struct UdpardRxSubscription subscription;
    int                         socket_fd[IFACE_COUNT];
};

struct Application
{
    // Common LibUDPard states.
    UdpardNodeID                 local_node_id;
    struct UdpardTx              udpard_tx;
    struct UdpardRxRPCDispatcher udpard_rpc_dispatcher;

    // Common sockets.
    // A deeply embedded system would normally use some minimal low-level UDP/IP stack instead of the Berkeley sockets.
    // LibUDPard needs one shared socket per network interface for sending all transfers, one for receiving all
    // RPC requests/responses per iface, and a dedicated socket per subscription per iface.
    int socket_fd_tx[IFACE_COUNT];
    int socket_fd_rpc_dispatcher[IFACE_COUNT];

    // Publishers.
    struct Publisher pub_heartbeat;
    struct Publisher pub_pnp_node_id_allocation;
    struct Publisher pub_data;

    // Subscribers.
    struct Subscriber sub_pnp_node_id_allocation;
    struct Subscriber sub_data;

    // RPC servers.
    struct UdpardRxRPCPort srv_get_node_info;
    struct UdpardRxRPCPort srv_register_list;
    struct UdpardRxRPCPort srv_register_access;
};

int main(void)
{
    return 0;
}
