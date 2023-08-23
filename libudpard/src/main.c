///                            ____                   ______            __          __
///                           / __ `____  ___  ____  / ____/_  ______  / /_  ____  / /
///                          / / / / __ `/ _ `/ __ `/ /   / / / / __ `/ __ `/ __ `/ /
///                         / /_/ / /_/ /  __/ / / / /___/ /_/ / /_/ / / / / /_/ / /
///                         `____/ .___/`___/_/ /_/`____/`__, / .___/_/ /_/`__,_/_/
///                             /_/                     /____/_/
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
///
/// This software is distributed under the terms of the MIT License.
/// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
/// Copyright Amazon.com Inc. or its affiliates.
/// SPDX-License-Identifier: MIT
/// Author: Pavel Kirienko <pavel@opencyphal.org>

// For clock_gettime().
#define _DEFAULT_SOURCE  // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

#include <udpard.h>
#include "memory_block.h"

// DSDL-generated types.
#include <uavcan/node/Heartbeat_1_0.h>
#include <uavcan/node/GetInfo_1_0.h>
#include <uavcan/pnp/NodeIDAllocationData_2_0.h>
#include <uavcan/_register/Access_1_0.h>

// POSIX API.
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

// Standard library.
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <time.h>

/// The number of network interfaces used. LibUDPard natively supports non-redundant interfaces,
/// doubly-redundant, and triply-redundant network interfaces for fault tolerance.
#define IFACE_REDUNDANCY_FACTOR 2
/// The maximum number of UDP datagrams enqueued in the TX queue at any given time.
#define TX_QUEUE_SIZE 50
/// The Cyphal/UDP specification recommends setting the TTL value of 16 hops.
#define UDP_TTL 16

/// This is used for sizing the memory pools for dynamic memory management.
/// We use a shared pool for both TX queues and for the RX buffers; the edge case is that we can have up to this
/// many items in the TX queue per iface or this many pending RX fragments per iface.
/// Remember that per the LibUDPard design, there is a dedicated TX pipeline per iface and shared RX pipelines for all
/// ifaces.
#define RESOURCE_LIMIT_PAYLOAD_FRAGMENTS ((TX_QUEUE_SIZE * IFACE_REDUNDANCY_FACTOR) + 50)
/// Each remote node emitting data on a given port that we are interested in requires us to allocate a small amount
/// of memory to keep certain state associated with that node. This is the maximum number of nodes we can handle.
#define RESOURCE_LIMIT_SESSIONS 1024

#define KILO 1000LL
#define MEGA (KILO * KILO)

typedef uint_least8_t byte_t;

/// Per the LibUDPard design, there is a dedicated TX pipeline per local network iface.
struct TxPipeline
{
    struct UdpardTx udpard_tx;
    int             socket_fd;
};

/// There is one RPC dispatcher in the entire application. It aggregates all RX RPC ports for all network ifaces.
/// The RPC dispatcher cannot be initialized unless the local node has a node-ID.
struct RPCDispatcher
{
    struct UdpardRxRPCDispatcher udpard_rpc_dispatcher;
    int                          socket_fd;
};

struct Publisher
{
    UdpardPortID        subject_id;
    enum UdpardPriority priority;
    UdpardMicrosecond   tx_timeout_usec;
    UdpardTransferID    transfer_id;
};

struct Subscriber
{
    struct UdpardRxSubscription subscription;
    int                         socket_fd[IFACE_REDUNDANCY_FACTOR];
};

struct Application
{
    UdpardMicrosecond started_at;

    /// This flag is raised when the node is requested to restart.
    bool restart_required;

    /// Common LibUDPard states.
    UdpardNodeID         local_node_id;
    struct TxPipeline    tx_pipeline[IFACE_REDUNDANCY_FACTOR];
    struct RPCDispatcher rpc_dispatcher;

    /// The local network interface addresses to use for this node.
    /// All communications are multicast, but multicast sockets need to be bound to a specific local address to
    /// tell the OS which ports to send/receive data via.
    struct in_addr ifaces[IFACE_REDUNDANCY_FACTOR];

    /// Publishers.
    struct Publisher pub_heartbeat;
    struct Publisher pub_pnp_node_id_allocation;
    struct Publisher pub_data;

    /// Subscribers.
    struct Subscriber sub_pnp_node_id_allocation;
    struct Subscriber sub_data;

    /// RPC servers.
    struct UdpardRxRPCPort srv_get_node_info;
    struct UdpardRxRPCPort srv_register_list;
    struct UdpardRxRPCPort srv_register_access;
};

/// A deeply embedded system should sample a microsecond-resolution non-overflowing 64-bit timer.
/// Here is a simple non-blocking implementation as an example:
/// https://github.com/PX4/sapog/blob/601f4580b71c3c4da65cc52237e62a/firmware/src/motor/realtime/motor_timer.c#L233-L274
/// Mind the difference between monotonic time and wall time. Monotonic time never changes rate or makes leaps,
/// it is therefore impossible to synchronize with an external reference. Wall time can be synchronized and therefore
/// it may change rate or make leap adjustments. The two kinds of time serve completely different purposes.
static UdpardMicrosecond getMonotonicMicroseconds(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        abort();
    }
    return (uint64_t) (ts.tv_sec * MEGA + ts.tv_nsec / KILO);
}

/// Returns the 128-bit unique-ID of the local node. This value is used in uavcan.node.GetInfo.Response and during the
/// plug-and-play node-ID allocation by uavcan.pnp.NodeIDAllocationData. The function is infallible.
static void getUniqueID(byte_t out[uavcan_node_GetInfo_Response_1_0_unique_id_ARRAY_CAPACITY_])
{
    // A real hardware node would read its unique-ID from some hardware-specific source (typically stored in ROM).
    // This example is a software-only node, so we store the unique-ID in a (read-only) register instead.
    uavcan_register_Value_1_0 value = {0};
    uavcan_register_Value_1_0_select_unstructured_(&value);
    // Populate the default; it is only used at the first run if there is no such register.
    for (size_t i = 0; i < uavcan_node_GetInfo_Response_1_0_unique_id_ARRAY_CAPACITY_; i++)
    {
        value.unstructured.value.elements[value.unstructured.value.count++] = (uint8_t) rand();  // NOLINT
    }
    // TODO FIXME READ THE REGISTER HERE
    assert(uavcan_register_Value_1_0_is_unstructured_(&value) &&
           value.unstructured.value.count == uavcan_node_GetInfo_Response_1_0_unique_id_ARRAY_CAPACITY_);
    memcpy(&out[0], &value.unstructured.value, uavcan_node_GetInfo_Response_1_0_unique_id_ARRAY_CAPACITY_);
}

/// Helpers for emitting transfers over all available interfaces.
static void publish(struct Application* const app,
                    struct Publisher* const   pub,
                    const size_t              payload_size,
                    const void* const         payload)
{
    const UdpardMicrosecond deadline = getMonotonicMicroseconds() + pub->tx_timeout_usec;
    for (size_t i = 0; i < IFACE_REDUNDANCY_FACTOR; i++)
    {
        (void) udpardTxPublish(&app->tx_pipeline[i].udpard_tx,
                               deadline,
                               pub->priority,
                               pub->subject_id,
                               &pub->transfer_id,
                               (struct UdpardPayload){.size = payload_size, .data = payload},
                               NULL);
    }
}

/// Invoked every second.
static void handle1HzLoop(struct Application* const app, const UdpardMicrosecond monotonic_time)
{
    const bool anonymous = app->local_node_id > UDPARD_NODE_ID_MAX;
    // Publish heartbeat every second unless the local node is anonymous. Anonymous nodes shall not publish heartbeat.
    if (!anonymous)
    {
        const uavcan_node_Heartbeat_1_0 heartbeat = {.uptime = (uint32_t) ((monotonic_time - app->started_at) / MEGA),
                                                     .mode   = {.value = uavcan_node_Mode_1_0_OPERATIONAL},
                                                     .health = {.value = uavcan_node_Health_1_0_NOMINAL},
                                                     .vendor_specific_status_code = 0};
        uint8_t                         serialized[uavcan_node_Heartbeat_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
        size_t                          serialized_size = sizeof(serialized);
        const int8_t err = uavcan_node_Heartbeat_1_0_serialize_(&heartbeat, &serialized[0], &serialized_size);
        assert(err >= 0);
        if (err >= 0)
        {
            publish(app, &app->pub_heartbeat, serialized_size, &serialized[0]);
        }
    }
    else  // If we don't have a node-ID, obtain one by publishing allocation request messages until we get a response.
    {
        // The Specification says that the allocation request publication interval shall be randomized.
        // We implement randomization by calling rand() at fixed intervals and comparing it against some threshold.
        // There are other ways to do it, of course. See the docs in the Specification or in the DSDL definition here:
        // https://github.com/OpenCyphal/public_regulated_data_types/blob/master/uavcan/pnp/8165.NodeIDAllocationData.2.0.dsdl
        // Note that a high-integrity/safety-certified application is unlikely to be able to rely on this feature.
        if (rand() > RAND_MAX / 2)  // NOLINT
        {
            uavcan_pnp_NodeIDAllocationData_2_0 msg = {.node_id = {.value = UINT16_MAX}};
            getUniqueID(msg.unique_id);
            uint8_t      serialized[uavcan_pnp_NodeIDAllocationData_2_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
            size_t       serialized_size = sizeof(serialized);
            const int8_t err = uavcan_pnp_NodeIDAllocationData_2_0_serialize_(&msg, &serialized[0], &serialized_size);
            assert(err >= 0);
            if (err >= 0)
            {
                // The response will arrive asynchronously eventually.
                publish(app, &app->pub_pnp_node_id_allocation, serialized_size, &serialized[0]);
            }
        }
    }
}

/// Invoked every 10 seconds.
static void handle01HzLoop(struct Application* const app, const UdpardMicrosecond monotonic_time)
{
    (void) app;
    (void) monotonic_time;
    // TODO FIXME
}

/// Sets up the instance of UdpardTx and configures the socket for it. Returns negative on error.
static int_fast32_t initTxPipeline(struct TxPipeline* const          self,
                                   const UdpardNodeID* const         local_node_id,
                                   const struct UdpardMemoryResource memory,
                                   const struct in_addr              local_iface)
{
    if (0 != udpardTxInit(&self->udpard_tx, local_node_id, TX_QUEUE_SIZE, memory))
    {
        return -EINVAL;
    }
    // Set up the TX socket for this iface.
    self->socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (self->socket_fd < 0)
    {
        return -errno;
    }
    if (bind(self->socket_fd,
             (struct sockaddr*) &(struct sockaddr_in){.sin_family = AF_INET, .sin_addr = local_iface, .sin_port = 0},
             sizeof(struct sockaddr_in)) != 0)
    {
        return -errno;
    }
    if (fcntl(self->socket_fd, F_SETFL, O_NONBLOCK) != 0)
    {
        return -errno;
    }
    const int ttl = UDP_TTL;
    if (setsockopt(self->socket_fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) != 0)
    {
        return -errno;
    }
    if (setsockopt(self->socket_fd,
                   IPPROTO_IP,
                   IP_MULTICAST_IF,
                   &(struct in_addr){.s_addr = local_iface.s_addr},
                   sizeof(struct in_addr)) != 0)
    {
        return -errno;
    }
    return 0;
}

int main(const int argc, const char* const argv[])
{
    if (argc != (1 + IFACE_REDUNDANCY_FACTOR))
    {
        (void) fprintf(stderr, "Usage: %s <iface-ip-0> <iface-ip-1> ...\n", argv[0]);
        return 1;
    }
    struct Application app = {.local_node_id = UDPARD_NODE_ID_UNSET};
    // Parse the iface addresses given via the command line. These will be used to decide which NICs to send/receive
    // data on. The addresses are in the form of IPv4 dotted quads.
    for (size_t i = 0; i < IFACE_REDUNDANCY_FACTOR; i++)
    {
        if (inet_pton(AF_INET, argv[1 + i], &app.ifaces[i]) == 0)
        {
            (void) fprintf(stderr, "Invalid IP address: %s\n", argv[1 + i]);
            return 1;
        }
    }
    // The block size values used here are derived from the sizes of the structs defined in LibUDPard and the MTU.
    // They may change when migrating between different versions of the library or when building the code for a
    // different platform, so it may be desirable to choose conservative values here (i.e. larger than necessary).
    MEMORY_BLOCK_ALLOCATOR_DEFINE(mem_session, 400, RESOURCE_LIMIT_SESSIONS);
    MEMORY_BLOCK_ALLOCATOR_DEFINE(mem_fragment, 88, RESOURCE_LIMIT_PAYLOAD_FRAGMENTS);
    MEMORY_BLOCK_ALLOCATOR_DEFINE(mem_payload, 2048, RESOURCE_LIMIT_PAYLOAD_FRAGMENTS);
    // Initialize the TX pipelines. We have one per local iface (unlike the RX pipelines which are shared).
    for (size_t i = 0; i < IFACE_REDUNDANCY_FACTOR; i++)
    {
        const int_fast32_t result =
            initTxPipeline(&app.tx_pipeline[i],
                           &app.local_node_id,
                           (struct UdpardMemoryResource){.user_reference = &mem_payload,  // Shared pool.
                                                         .allocate       = &memoryBlockAllocate,
                                                         .deallocate     = &memoryBlockDeallocate},
                           app.ifaces[i]);
        if (result < 0)
        {
            (void) fprintf(stderr, "Failed to initialize TX pipeline for iface %zu: %li\n", i, -result);
            return 1;
        }
    }
    // Initialize the publishers. They are not dependent on the local node-ID value.
    // Heartbeat.
    app.pub_heartbeat.priority        = UdpardPriorityNominal;
    app.pub_heartbeat.subject_id      = uavcan_node_Heartbeat_1_0_FIXED_PORT_ID_;
    app.pub_heartbeat.tx_timeout_usec = 1 * MEGA;
    // PnP node-ID allocation.
    app.pub_pnp_node_id_allocation.priority        = UdpardPrioritySlow;
    app.pub_pnp_node_id_allocation.subject_id      = uavcan_pnp_NodeIDAllocationData_2_0_FIXED_PORT_ID_;
    app.pub_pnp_node_id_allocation.tx_timeout_usec = 1 * MEGA;
    // Data.
    app.pub_data.priority        = UdpardPriorityFast;
    app.pub_data.subject_id      = 1234;  // TODO FIXME pull the register API. NOLINT
    app.pub_data.tx_timeout_usec = 50 * KILO;

    // TODO
    (void) mem_session;
    (void) mem_fragment;

    // Main loop.
    app.started_at                       = getMonotonicMicroseconds();
    UdpardMicrosecond next_1_hz_iter_at  = app.started_at + MEGA;
    UdpardMicrosecond next_01_hz_iter_at = app.started_at + (MEGA * 10);
    while (!app.restart_required)
    {
        // Run a trivial scheduler polling the loops that run the business logic.
        const UdpardMicrosecond monotonic_time = getMonotonicMicroseconds();
        if (monotonic_time >= next_1_hz_iter_at)
        {
            next_1_hz_iter_at += MEGA;
            handle1HzLoop(&app, monotonic_time);
        }
        if (monotonic_time >= next_01_hz_iter_at)
        {
            next_01_hz_iter_at += (MEGA * 10);
            handle01HzLoop(&app, monotonic_time);
        }

        // Transmit pending frames from the prioritized TX queues managed by libudpard.
        for (size_t i = 0; i < IFACE_REDUNDANCY_FACTOR; i++)
        {
            struct TxPipeline* const   pipe = &app.tx_pipeline[i];
            const struct UdpardTxItem* tqi  = udpardTxPeek(&pipe->udpard_tx);  // Find the highest-priority datagram.
            while (tqi != NULL)
            {
                // Attempt transmission only if the frame is not yet timed out while waiting in the TX queue.
                // Otherwise, just drop it and move on to the next one.
                if ((tqi->deadline_usec == 0) || (tqi->deadline_usec > monotonic_time))
                {
                    const ssize_t send_res =
                        sendto(pipe->socket_fd,
                               tqi->datagram_payload.data,
                               tqi->datagram_payload.size,
                               MSG_DONTWAIT,
                               (struct sockaddr*) &(struct sockaddr_in){.sin_family = AF_INET,
                                                                        .sin_addr   = {tqi->destination.ip_address},
                                                                        .sin_port   = tqi->destination.udp_port},
                               sizeof(struct sockaddr_in));
                    if (send_res < 0)
                    {
                        if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
                        {
                            break;  // No more space in the TX buffer, try again later.
                        }
                        (void) fprintf(stderr, "Iface #%zu send error: %i [%s]\n", i, errno, strerror(errno));
                        // Datagram will be discarded.
                    }
                }
                udpardTxFree(pipe->udpard_tx.memory, udpardTxPop(&pipe->udpard_tx, tqi));
                tqi = udpardTxPeek(&pipe->udpard_tx);
            }
        }

        // Receive pending frames from the RX sockets of all network interfaces and feed them into the library.
        // Unblock early if TX sockets become writable and the TX queues are not empty.
        // FIXME TODO
        usleep(1000);
    }

    return 0;
}
