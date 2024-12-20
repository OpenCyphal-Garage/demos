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
/// The following BPF expression can be used to filter Cyphal/UDP traffic (e.g., in Wireshark):
///
///     udp and dst net 239.0.0.0 mask 255.0.0.0 and dst port 9382
///
/// This software is distributed under the terms of the MIT License.
/// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
/// Copyright Amazon.com Inc. or its affiliates.
/// SPDX-License-Identifier: MIT
/// Author: Pavel Kirienko <pavel@opencyphal.org>

// For clock_gettime().
#define _DEFAULT_SOURCE  // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

#include "register.h"
#include "memory_block.h"
#include "storage.h"
#include "udp.h"
#include <udpard.h>

// DSDL-generated types.
#include <uavcan/node/Heartbeat_1_0.h>
#include <uavcan/node/port/List_1_0.h>
#include <uavcan/node/GetInfo_1_0.h>
#include <uavcan/node/ExecuteCommand_1_1.h>
#include <uavcan/_register/Access_1_0.h>
#include <uavcan/_register/List_1_0.h>
#include <uavcan/pnp/NodeIDAllocationData_2_0.h>
#include <uavcan/primitive/array/Real32_1_0.h>

// POSIX API.
#include <unistd.h>  // execve

// Standard library.
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>

/// By default, only the local loopback interface is used.
/// The connectivity can be changed after the node is launched via the register API.
/// LibUDPard natively supports non-redundant, doubly-redundant, and triply-redundant network interfaces
/// for fault tolerance.
/// A deeply embedded application may instead run a dynamic DHCP client to obtain its address(es) and use that,
/// or in the simplest case it may simply default to some hard-coded address (likely Class C).
#define DEFAULT_IFACE "127.0.0.1"

/// The maximum number of UDP datagrams enqueued in the TX queue at any given time.
#define TX_QUEUE_SIZE 50
/// Maximum expected incoming datagram size.
#define RX_BUFFER_SIZE 2000

/// This is used for sizing the memory pools for dynamic memory management.
/// We use a shared pool for both TX queues and for the RX buffers; the edge case is that we can have up to this
/// many items in the TX queue per iface or this many pending RX fragments per iface.
/// Remember that per the LibUDPard design, there is a dedicated TX pipeline per iface and shared RX pipelines for all
/// ifaces.
#define RESOURCE_LIMIT_PAYLOAD_FRAGMENTS ((TX_QUEUE_SIZE * UDPARD_NETWORK_INTERFACE_COUNT_MAX) + 50)
/// Each remote node emitting data on a given port that we are interested in requires us to allocate a small amount
/// of memory to keep certain state associated with that node. This is the maximum number of nodes we can handle.
#define RESOURCE_LIMIT_SESSIONS 1024

#define KILO 1000LL
#define MEGA (KILO * KILO)

typedef uint_least8_t byte_t;

/// Per the LibUDPard design, there is a dedicated TX pipeline per local network iface.
/// A single pipeline is used for all kinds of outgoing transfers: message publications, requests, and responses.
struct TxPipeline
{
    struct UdpardTx udpard_tx;
    UDPTxHandle     io;  ///< The socket that is used for transmitting on this iface.
};

/// There is one RPC dispatcher in the entire application. It aggregates all RX RPC ports for all network ifaces.
/// The RPC dispatcher cannot be initialized unless the local node has a node-ID.
struct RPCDispatcher
{
    struct UdpardRxRPCDispatcher udpard_rpc_dispatcher;
    UDPRxHandle                  io[UDPARD_NETWORK_INTERFACE_COUNT_MAX];
};

/// There needs to be one instance of this type per subject the application wants to publish on.
/// E.g., for the heartbeat, etc.
struct Publisher
{
    UdpardPortID        subject_id;  ///< If invalid, this publisher is disabled.
    enum UdpardPriority priority;
    UdpardMicrosecond   tx_timeout_usec;
    UdpardTransferID    transfer_id;
};

/// There needs to be one instance of this type per subject the application wants to subscribe to.
struct Subscriber;
typedef void (*SubscriberCallback)(struct Subscriber* const self, struct UdpardRxTransfer* const transfer);
struct Subscriber
{
    struct UdpardRxSubscription subscription;
    UDPRxHandle io[UDPARD_NETWORK_INTERFACE_COUNT_MAX];  ///< Each subscription needs a separate socket per iface.
    bool        enabled;                                 ///< True if in use.
    /// The caller will free the transfer payload after this callback returns.
    /// If the callback would like to prevent that (e.g., the payload is needed for later processing),
    /// it can erase the payload pointers from the transfer object.
    SubscriberCallback handler;
    void*              user_reference;
};

/// There needs to be one instance of this type per RPC service the application wants to serve.
/// No sockets are needed for this one as the reception is managed by the RPC dispatcher (see above) and
/// transmission is managed by the TX pipelines.
struct RPCServer;
// The TX pipelines are passed to let the handler transmit the response.
typedef void (*RPCServerCallback)(struct RPCServer* const           self,
                                  struct UdpardRxRPCTransfer* const request_transfer,
                                  const size_t                      iface_count,
                                  struct TxPipeline* const          tx);
struct RPCServer
{
    struct UdpardRxRPCPort base;
    bool                   enabled;  ///< True if in use.
    /// The caller will free the transfer payload after this callback returns.
    /// If the callback would like to prevent that (e.g., the payload is needed for later processing),
    /// it can erase the payload pointers from the transfer object.
    RPCServerCallback handler;
    void*             user_reference;
};

/// For more information about registers, please refer to "register.h" and the standard DSDL definition
/// "uavcan.register.Access".
/// The port register sets are helpers to simplify the implementation of port configuration/introspection registers.
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

/// A set of registers specific to this application. Feel free to extend.
struct ApplicationRegisters
{
    struct Register              node_id;           ///< uavcan.node.id             : natural16[1]
    struct Register              node_description;  ///< uavcan.node.description    : string
    struct Register              udp_iface;         ///< uavcan.udp.iface           : string
    struct Register              udp_dscp;          ///< uavcan.udp.dscp            : natural8[8]
    struct Register              mem_info;          ///< A simple diagnostic register for viewing the memory usage.
    struct PublisherRegisterSet  pub_data;
    struct SubscriberRegisterSet sub_data;
};

/// These different memory allocators are needed for LibUDPard.
/// They can be replaced with a single heap allocator, but for the sake of a good illustration we use simple
/// fixed-size block pools instead.
struct ApplicationMemory
{
    struct
    {
        struct UdpardMemoryResource session;
        struct UdpardMemoryResource fragment;
        struct UdpardMemoryResource payload;
    } rx;
    struct UdpardTxMemoryResources tx;
};

/// The god object.
struct Application
{
    UdpardMicrosecond started_at;

    /// The unique-ID of the local node. This value is initialized once at startup.
    byte_t unique_id[uavcan_node_GetInfo_Response_1_0_unique_id_ARRAY_CAPACITY_];

    /// These flags are raised in response to external requests.
    bool restart_required;
    bool factory_reset_required;

    struct ApplicationMemory memory;

    /// Common LibUDPard states.
    uint_fast8_t         iface_count;
    UdpardNodeID         local_node_id;
    struct TxPipeline    tx_pipeline[UDPARD_NETWORK_INTERFACE_COUNT_MAX];
    struct RPCDispatcher rpc_dispatcher;

    /// The local network interface addresses to use for this node.
    /// All communications are multicast, but multicast sockets need to be bound to a specific local address to
    /// tell the OS which ports to send/receive data via.
    uint32_t ifaces[UDPARD_NETWORK_INTERFACE_COUNT_MAX];

    /// Publishers.
    struct Publisher pub_heartbeat;
    struct Publisher pub_port_list;
    struct Publisher pub_pnp_node_id_allocation;
    struct Publisher pub_data;  // uavcan_primitive_array_Real32_1_0

    /// Subscribers.
    struct Subscriber sub_pnp_node_id_allocation;
    struct Subscriber sub_data;  // uavcan_primitive_array_Real32_1_0

    /// RPC servers.
    struct RPCServer srv_get_node_info;
    struct RPCServer srv_execute_command;
    struct RPCServer srv_register_list;
    struct RPCServer srv_register_access;

    /// Registers.
    struct Register*            reg_root;  ///< The root of the register tree.
    struct ApplicationRegisters reg;
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
    // This example is a software-only node, so we generate the UID at first launch and store it permanently.
    static const char* const Key  = ".unique_id";
    size_t                   size = uavcan_node_GetInfo_Response_1_0_unique_id_ARRAY_CAPACITY_;
    if ((!storageGet(Key, &size, out)) || (size != uavcan_node_GetInfo_Response_1_0_unique_id_ARRAY_CAPACITY_))
    {
        // Populate the default; it is only used at the first run.
        for (size_t i = 0; i < uavcan_node_GetInfo_Response_1_0_unique_id_ARRAY_CAPACITY_; i++)
        {
            out[i] = (byte_t) rand();  // NOLINT
        }
        if (!storagePut(Key, uavcan_node_GetInfo_Response_1_0_unique_id_ARRAY_CAPACITY_, out))
        {
            abort();  // The node cannot function if the storage system is not available.
        }
    }
}

/// The dispatcher passed here shall already be initialized.
static int16_t startRPCDispatcher(struct RPCDispatcher* const self,
                                  const UdpardNodeID          local_node_id,
                                  const size_t                iface_count,
                                  const uint32_t* const       ifaces)
{
    struct UdpardUDPIPEndpoint udp_ip_endpoint = {0};
    int16_t res = (int16_t) udpardRxRPCDispatcherStart(&self->udpard_rpc_dispatcher, local_node_id, &udp_ip_endpoint);
    if (res == 0)
    {
        for (size_t i = 0; i < iface_count; i++)
        {
            res = udpRxInit(&self->io[i], ifaces[i], udp_ip_endpoint.ip_address, udp_ip_endpoint.udp_port);
            (void) fprintf(stderr,
                           "RPCDispatcher socket iface %08x#%zu endpoint %08x:%u result %i\n",
                           ifaces[i],
                           i,
                           udp_ip_endpoint.ip_address,
                           udp_ip_endpoint.udp_port,
                           res);
            if (res < 0)
            {
                break;
            }
        }
    }
    return res;
}

static int16_t initRPCServer(struct RPCServer* const             self,
                             struct UdpardRxRPCDispatcher* const dispatcher,
                             const UdpardPortID                  service_id,
                             const size_t                        request_extent,
                             const RPCServerCallback             handler)
{
    (void) memset(self, 0, sizeof(*self));
    self->enabled = service_id <= UDPARD_SUBJECT_ID_MAX;
    int16_t res   = 0;
    if (self->enabled)
    {
        self->handler = handler;
        res = (int16_t) udpardRxRPCDispatcherListen(dispatcher, &self->base, service_id, true, request_extent);
    }
    return res;
}

/// Invalid subject-ID is not considered an error but rather as a disabled publisher.
/// The application needs to check whether the subject-ID is valid before publishing.
static void initPublisher(struct Publisher* const self,
                          const uint8_t           priority,
                          const uint16_t          subject_id,
                          const UdpardMicrosecond tx_timeout_usec)
{
    (void) memset(self, 0, sizeof(*self));
    self->priority   = (priority <= UDPARD_PRIORITY_MAX) ? ((enum UdpardPriority) priority) : UdpardPriorityOptional;
    self->subject_id = subject_id;
    self->tx_timeout_usec = tx_timeout_usec;
}

/// Returns negative error code on failure.
static int16_t initSubscriber(struct Subscriber* const             self,
                              const UdpardPortID                   subject_id,
                              const size_t                         extent,
                              const SubscriberCallback             handler,
                              const struct UdpardRxMemoryResources memory,
                              const size_t                         iface_count,
                              const uint32_t* const                ifaces)
{
    (void) memset(self, 0, sizeof(*self));
    self->enabled = subject_id <= UDPARD_SUBJECT_ID_MAX;
    int16_t res   = 0;
    if (self->enabled)
    {
        res = (int16_t) udpardRxSubscriptionInit(&self->subscription, subject_id, extent, memory);
        if (res >= 0)
        {
            self->handler = handler;
            for (size_t i = 0; i < iface_count; i++)
            {
                res = udpRxInit(&self->io[i],
                                ifaces[i],
                                self->subscription.udp_ip_endpoint.ip_address,
                                self->subscription.udp_ip_endpoint.udp_port);
                (void) fprintf(stderr,
                               "Subscriber socket iface %08x#%zu endpoint %08x:%u result %i\n",
                               ifaces[i],
                               i,
                               self->subscription.udp_ip_endpoint.ip_address,
                               self->subscription.udp_ip_endpoint.udp_port,
                               res);
                if (res < 0)
                {
                    break;
                }
            }
        }
    }
    return res;
}

/// A helper for publishing a message over all available redundant network interfaces.
static void publish(const size_t             iface_count,
                    struct TxPipeline* const tx,
                    struct Publisher* const  pub,
                    const size_t             payload_size,
                    const void* const        payload)
{
    const UdpardMicrosecond deadline = getMonotonicMicroseconds() + pub->tx_timeout_usec;
    for (size_t i = 0; i < iface_count; i++)
    {
        (void) udpardTxPublish(&tx[i].udpard_tx,
                               deadline,
                               pub->priority,
                               pub->subject_id,
                               pub->transfer_id,
                               (struct UdpardPayload){.size = payload_size, .data = payload},
                               NULL);
    }
    pub->transfer_id++;
}

/// A helper for transmitting an RPC-service response over all available redundant network interfaces.
/// The original request transfer is needed to extract the response metadata such as the transfer-ID and client node-ID.
static void respond(const size_t                      iface_count,
                    struct TxPipeline* const          tx,
                    struct UdpardRxRPCTransfer* const culprit,
                    const size_t                      payload_size,
                    const void* const                 payload)
{
    const UdpardMicrosecond deadline = getMonotonicMicroseconds() + MEGA;
    for (size_t i = 0; i < iface_count; i++)
    {
        (void) udpardTxRespond(&tx[i].udpard_tx,
                               deadline,
                               culprit->base.priority,
                               culprit->service_id,
                               culprit->base.source_node_id,
                               culprit->base.transfer_id,
                               (struct UdpardPayload){.size = payload_size, .data = payload},
                               NULL);
    }
}

static void cbOnNodeIDAllocationData(struct Subscriber* const self, struct UdpardRxTransfer* const transfer)
{
    assert((self != NULL) && (transfer != NULL));
    struct Application* const app = self->user_reference;
    assert(app != NULL);
    // Remember that anonymous transfers are stateless and are not deduplicated, so we have to check if the node-ID
    // is already allocated.
    if ((transfer->source_node_id <= UDPARD_NODE_ID_MAX) && (app->local_node_id == UDPARD_NODE_ID_UNSET))
    {
        byte_t payload[uavcan_pnp_NodeIDAllocationData_2_0_EXTENT_BYTES_];
        size_t payload_size = udpardGather(transfer->payload, sizeof(payload), &payload[0]);
        uavcan_pnp_NodeIDAllocationData_2_0 obj;
        if (uavcan_pnp_NodeIDAllocationData_2_0_deserialize_(&obj, &payload[0], &payload_size) >= 0)
        {
            if ((obj.node_id.value <= UDPARD_NODE_ID_MAX) &&
                (0 == memcmp(&obj.unique_id[0], &app->unique_id[0], sizeof(app->unique_id))))
            {
                app->local_node_id                                 = obj.node_id.value;
                app->reg.node_id.value.natural16.value.elements[0] = obj.node_id.value;
                (void) fprintf(stderr,
                               "Allocated NodeID %u by allocator %u\n",
                               app->local_node_id,
                               transfer->source_node_id);
                // Optionally we can unsubscribe to reduce memory utilization, as we no longer need this subject.
                // Some high-integrity applications may not be able to do that, though.
                self->handler = NULL;
                self->enabled = false;
                for (size_t i = 0; i < app->iface_count; i++)
                {
                    udpRxClose(&self->io[i]);
                }
                udpardRxSubscriptionFree(&self->subscription);
                // Now that we know our node-ID, we can initialize the RPC dispatcher.
                assert(app->local_node_id <= UDPARD_NODE_ID_MAX);
                assert(app->rpc_dispatcher.udpard_rpc_dispatcher.local_node_id == UDPARD_NODE_ID_UNSET);
                const int16_t rpc_start_res = startRPCDispatcher(&app->rpc_dispatcher,  //
                                                                 app->local_node_id,
                                                                 app->iface_count,
                                                                 &app->ifaces[0]);
                if (rpc_start_res < 0)
                {
                    (void) fprintf(stderr, "RPC dispatcher start failed: %i\n", rpc_start_res);
                }
            }  // Otherwise, it's a response destined to another node, or it's a malformed message.
        }  // Otherwise, the message is malformed.
    }  // Otherwise, it's a request from another allocation client node, or we already have a node-ID.
}

static void cbOnMyData(struct Subscriber* const self, struct UdpardRxTransfer* const transfer)
{
    (void) fprintf(stderr,
                   "Received my_data with transfer-ID %" PRIu64 " from node %u\n",
                   transfer->transfer_id,
                   transfer->source_node_id);
    uavcan_primitive_array_Real32_1_0 msg = {0};
    byte_t                            payload[uavcan_primitive_array_Real32_1_0_EXTENT_BYTES_];
    size_t                            payload_size = udpardGather(transfer->payload, sizeof(payload), &payload[0]);
    if (uavcan_primitive_array_Real32_1_0_deserialize_(&msg, &payload[0], &payload_size) >= 0)
    {
        // Process the received data. In this demo we reverse the array and publish the result.
        for (size_t i = 0; i < msg.value.count / 2; i++)
        {
            const float tmp                             = msg.value.elements[i];
            msg.value.elements[i]                       = msg.value.elements[msg.value.count - i - 1];
            msg.value.elements[msg.value.count - i - 1] = tmp;
        }
        // Serialize and publish.
        struct Application* const app = self->user_reference;
        if (app->pub_data.subject_id <= UDPARD_SUBJECT_ID_MAX)
        {
            byte_t serialized[uavcan_primitive_array_Real32_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
            size_t serialized_size = sizeof(serialized);
            if (uavcan_primitive_array_Real32_1_0_serialize_(&msg, &serialized[0], &serialized_size) >= 0)
            {
                assert(app != NULL);
                publish(app->iface_count, &app->tx_pipeline[0], &app->pub_data, serialized_size, &serialized[0]);
            }
            else
            {
                assert(false);
            }
        }
        else
        {
            (void) fprintf(stderr, "Data publisher is not enabled\n");
        }
    }
    else
    {
        (void) fprintf(stderr, "Malformed uavcan.primitive.array.Real32.1.0\n");
    }
}

static void cbOnGetNodeInfoRequest(struct RPCServer* const           self,
                                   struct UdpardRxRPCTransfer* const request_transfer,
                                   const size_t                      iface_count,
                                   struct TxPipeline* const          tx)
{
    assert((self != NULL) && (request_transfer != NULL) && (tx != NULL));
    struct Application* const app = self->user_reference;
    assert(app != NULL);
    uavcan_node_GetInfo_Response_1_0 resp = {0};
    resp.protocol_version.major           = UDPARD_CYPHAL_SPECIFICATION_VERSION_MAJOR;
    resp.protocol_version.minor           = UDPARD_CYPHAL_SPECIFICATION_VERSION_MINOR;
    resp.software_version.major           = VERSION_MAJOR;
    resp.software_version.minor           = VERSION_MINOR;
    resp.software_vcs_revision_id         = VCS_REVISION_ID;
    (void) memcpy(&resp.unique_id[0], &app->unique_id[0], sizeof(resp.unique_id));
    resp.name.count = strlen(NODE_NAME);
    (void) memcpy(&resp.name.elements[0], NODE_NAME, resp.name.count);
    uint8_t serialized[uavcan_node_GetInfo_Response_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t  serialized_size = sizeof(serialized);
    if (uavcan_node_GetInfo_Response_1_0_serialize_(&resp, &serialized[0], &serialized_size) >= 0)
    {
        respond(iface_count, tx, request_transfer, serialized_size, &serialized[0]);
    }
    else
    {
        assert(false);
    }
}

static void cbOnExecuteCommandRequest(struct RPCServer* const           self,
                                      struct UdpardRxRPCTransfer* const request_transfer,
                                      const size_t                      iface_count,
                                      struct TxPipeline* const          tx)
{
    assert((self != NULL) && (request_transfer != NULL) && (tx != NULL));
    struct Application* const app = self->user_reference;
    assert(app != NULL);
    uavcan_node_ExecuteCommand_Request_1_1 request = {0};
    byte_t                                 payload[uavcan_node_ExecuteCommand_Request_1_1_EXTENT_BYTES_];
    size_t payload_size = udpardGather(request_transfer->base.payload, sizeof(payload), &payload[0]);
    if (uavcan_node_ExecuteCommand_Request_1_1_deserialize_(&request, &payload[0], &payload_size) >= 0)
    {
        uavcan_node_ExecuteCommand_Response_1_1 resp = {
            .status = uavcan_node_ExecuteCommand_Response_1_1_STATUS_INTERNAL_ERROR,
        };
        switch (request.command)
        {
        case uavcan_node_ExecuteCommand_Request_1_1_COMMAND_RESTART:
        case uavcan_node_ExecuteCommand_Request_1_1_COMMAND_STORE_PERSISTENT_STATES:
        {
            // The registers are stored only during shutdown (i.e., before reboot),
            // so these two commands are handled identically in this implementation.
            app->restart_required = true;
            resp.status           = uavcan_node_ExecuteCommand_Response_1_1_STATUS_SUCCESS;
            break;
        }
        case uavcan_node_ExecuteCommand_Request_1_1_COMMAND_FACTORY_RESET:
        {
            app->factory_reset_required = true;  // Restart is needed to complete the factory reset.
            resp.status                 = uavcan_node_ExecuteCommand_Response_1_1_STATUS_SUCCESS;
            break;
        }
#ifdef EVIL           // This example is disabled as it is insecure; however, it may be useful for advanced diagnostics.
        case 0xE71L:  // NOLINT(readability-magic-numbers) Example of a custom command.
        {
            char buf[uavcan_node_ExecuteCommand_Request_1_1_parameter_ARRAY_CAPACITY_ + 1];
            (void) memcpy(&buf[0], &request.parameter.elements[0], request.parameter.count);
            buf[request.parameter.count] = '\0';
            // NOLINTNEXTLINE(cert-env33-c) insecure.
            resp.status = (system(&buf[0]) == 0)  // You get the idea.
                              ? uavcan_node_ExecuteCommand_Response_1_1_STATUS_SUCCESS
                              : uavcan_node_ExecuteCommand_Response_1_1_STATUS_FAILURE;
            break;
        }
#endif
        default:
        {
            resp.status = uavcan_node_ExecuteCommand_Response_1_1_STATUS_BAD_COMMAND;
            break;
        }
        }
        byte_t resp_serialized[uavcan_node_ExecuteCommand_Response_1_1_SERIALIZATION_BUFFER_SIZE_BYTES_];
        size_t resp_serialized_size = sizeof(resp_serialized);
        if (uavcan_node_ExecuteCommand_Response_1_1_serialize_(&resp, &resp_serialized[0], &resp_serialized_size) >= 0)
        {
            respond(iface_count, tx, request_transfer, resp_serialized_size, &resp_serialized[0]);
        }
        else
        {
            assert(false);
        }
    }
    else
    {
        (void) fprintf(stderr, "Malformed uavcan.node.ExecuteCommand.Request\n");
    }
}

static void cbOnRegisterListRequest(struct RPCServer* const           self,
                                    struct UdpardRxRPCTransfer* const request_transfer,
                                    const size_t                      iface_count,
                                    struct TxPipeline* const          tx)
{
    assert((self != NULL) && (request_transfer != NULL) && (tx != NULL));
    uavcan_register_List_Request_1_0 request = {0};
    byte_t                           payload[uavcan_register_List_Request_1_0_EXTENT_BYTES_];
    size_t payload_size = udpardGather(request_transfer->base.payload, sizeof(payload), &payload[0]);
    if (uavcan_register_List_Request_1_0_deserialize_(&request, &payload[0], &payload_size) >= 0)
    {
        const struct Register* const reg = registerFindByIndex((struct Register*) self->user_reference, request.index);
        uavcan_register_List_Response_1_0 resp = {0};
        if (reg != NULL)
        {
            resp.name.name.count = strlen(reg->name);
            (void) memcpy(&resp.name.name.elements[0], reg->name, resp.name.name.count);
        }
        byte_t resp_serialized[uavcan_register_List_Response_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
        size_t resp_serialized_size = sizeof(resp_serialized);
        if (uavcan_register_List_Response_1_0_serialize_(&resp, &resp_serialized[0], &resp_serialized_size) >= 0)
        {
            respond(iface_count, tx, request_transfer, resp_serialized_size, &resp_serialized[0]);
        }
        else
        {
            assert(false);
        }
    }
    else
    {
        (void) fprintf(stderr, "Malformed uavcan.register.List.Request\n");
    }
}

static void cbOnRegisterAccessRequest(struct RPCServer* const           self,
                                      struct UdpardRxRPCTransfer* const request_transfer,
                                      const size_t                      iface_count,
                                      struct TxPipeline* const          tx)
{
    assert((self != NULL) && (request_transfer != NULL) && (tx != NULL));
    uavcan_register_Access_Request_1_0 request = {0};
    byte_t                             payload[uavcan_register_Access_Request_1_0_EXTENT_BYTES_];
    size_t payload_size = udpardGather(request_transfer->base.payload, sizeof(payload), &payload[0]);
    if (uavcan_register_Access_Request_1_0_deserialize_(&request, &payload[0], &payload_size) >= 0)
    {
        // Locate the register by name.
        char name_buf[uavcan_register_Name_1_0_name_ARRAY_CAPACITY_ + 1];
        (void) memcpy(&name_buf[0], &request.name.name.elements[0], request.name.name.count);
        name_buf[request.name.name.count] = '\0';
        struct Register* const reg        = registerFindByName((struct Register*) self->user_reference, &name_buf[0]);
        // Prepare the response with the default values.
        uavcan_register_Access_Response_1_0 resp = {0};
        uavcan_register_Value_1_0_select_empty_(&resp.value);
        if (reg != NULL)
        {
            // Modify the register if modification is requested and is possible.
            if (reg->remote_mutable)
            {
                (void) registerAssign(&reg->value, &request.value);  // No-op if the source is empty.
            }
            // Read the final value.
            resp.timestamp.microsecond = getMonotonicMicroseconds();
            if (reg->getter != NULL)
            {
                reg->value = reg->getter(reg);
            }
            resp.value      = reg->value;
            resp.persistent = reg->persistent;
            resp._mutable   = reg->remote_mutable;
        }
        // Serialize and send the response.
        byte_t resp_serialized[uavcan_register_Access_Response_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
        size_t resp_serialized_size = sizeof(resp_serialized);
        if (uavcan_register_Access_Response_1_0_serialize_(&resp, &resp_serialized[0], &resp_serialized_size) >= 0)
        {
            respond(iface_count, tx, request_transfer, resp_serialized_size, &resp_serialized[0]);
        }
        else
        {
            assert(false);
        }
    }
    else
    {
        (void) fprintf(stderr, "Malformed uavcan.register.Access.Request\n");
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
            publish(app->iface_count, &app->tx_pipeline[0], &app->pub_heartbeat, serialized_size, &serialized[0]);
        }
    }
    else  // If we don't have a node-ID, obtain one by publishing allocation request messages until we get a response.
    {
        // The Specification says that the allocation request publication interval shall be randomized.
        // We implement randomization by calling rand() at fixed intervals and comparing it against some threshold.
        // There are other ways to do it, of course. See the docs in the Specification or in the DSDL definition here:
        // https://github.com/OpenCyphal/public_regulated_data_types/blob/master/uavcan/pnp/8165.NodeIDAllocationData.2.0.dsdl
        // Note that a high-integrity/safety-certified application is unlikely to be able to rely on this feature.
        if (rand() > (RAND_MAX / 2))  // NOLINT
        {
            uavcan_pnp_NodeIDAllocationData_2_0 msg = {.node_id = {.value = UINT16_MAX}};
            (void) memcpy(&msg.unique_id[0], &app->unique_id[0], sizeof(app->unique_id));
            uint8_t      serialized[uavcan_pnp_NodeIDAllocationData_2_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
            size_t       serialized_size = sizeof(serialized);
            const int8_t err = uavcan_pnp_NodeIDAllocationData_2_0_serialize_(&msg, &serialized[0], &serialized_size);
            assert(err >= 0);
            if (err >= 0)
            {
                // The response will arrive asynchronously eventually.
                publish(app->iface_count,
                        &app->tx_pipeline[0],
                        &app->pub_pnp_node_id_allocation,
                        serialized_size,
                        &serialized[0]);
            }
        }
    }
}

/// Invoked every 10 seconds.
static void handle01HzLoop(struct Application* const app, const UdpardMicrosecond monotonic_time)
{
    assert(app != NULL);
    (void) monotonic_time;
    // Publish uavcan.node.port.List periodically. This standard message is used to inform other network participants
    // about the topics we publish/subscribe to and the RPC-services we invoke and serve.
    // This information is important for diagnostics and can also be leveraged by self-configuring network bridges;
    // e.g., for routing data between CAN and UDP segments as shown at
    // https://forum.opencyphal.org/t/cyphal-udp-routing-over-multiple-networks/1657/7.
    if (app->local_node_id <= UDPARD_NODE_ID_MAX)
    {
        uavcan_node_port_List_1_0 msg;  // Default initialization skipped because the struct is large.
        // Publishers.
        {
            uavcan_node_port_SubjectIDList_1_0_select_sparse_list_(&msg.publishers);
            msg.publishers.sparse_list.count = 0;
            msg.publishers.sparse_list.elements[msg.publishers.sparse_list.count++].value =
                app->pub_heartbeat.subject_id;
            msg.publishers.sparse_list.elements[msg.publishers.sparse_list.count++].value =
                app->pub_port_list.subject_id;
            // The PnP publisher is not used when a node-ID is available.
            if (app->pub_data.subject_id <= UDPARD_SUBJECT_ID_MAX)
            {
                msg.publishers.sparse_list.elements[msg.publishers.sparse_list.count++].value =
                    app->pub_data.subject_id;
            }
        }
        // Subscribers.
        {
            uavcan_node_port_SubjectIDList_1_0_select_sparse_list_(&msg.subscribers);
            msg.subscribers.sparse_list.count = 0;
            if (app->sub_pnp_node_id_allocation.enabled)
            {
                msg.subscribers.sparse_list.elements[msg.subscribers.sparse_list.count++].value =
                    uavcan_pnp_NodeIDAllocationData_2_0_FIXED_PORT_ID_;
            }
            if (app->sub_data.enabled)
            {
                msg.subscribers.sparse_list.elements[msg.subscribers.sparse_list.count++].value =
                    app->reg.sub_data.base.id.value.natural16.value.elements[0];
            }
        }
        // RPC clients.
        {
            (void) memset(msg.clients.mask_bitpacked_, 0x00, sizeof(msg.clients.mask_bitpacked_));
            // No clients in this application.
        }
        // RPC servers.
        {
            (void) memset(msg.servers.mask_bitpacked_, 0x00, sizeof(msg.clients.mask_bitpacked_));
            const struct RPCServer* const srv[] = {&app->srv_get_node_info,
                                                   &app->srv_execute_command,
                                                   &app->srv_register_list,
                                                   &app->srv_register_access};
            for (size_t i = 0; i < (sizeof(srv) / sizeof(srv[0])); i++)
            {
                if (srv[i]->enabled)
                {
                    (void) nunavutSetBit(msg.servers.mask_bitpacked_,
                                         sizeof(msg.servers.mask_bitpacked_),
                                         srv[i]->base.service_id,
                                         true);
                }
            }
        }
        // Serialize and publish.
        uint8_t serialized[uavcan_node_port_List_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
        size_t  serialized_size = sizeof(serialized);
        if (uavcan_node_port_List_1_0_serialize_(&msg, &serialized[0], &serialized_size) >= 0)
        {
            publish(app->iface_count, &app->tx_pipeline[0], &app->pub_port_list, serialized_size, &serialized[0]);
        }
        else
        {
            assert(false);
        }
    }
}

/// Checks whether any pending TX datagrams can be written into sockets.
/// This is non-blocking, returns immediately.
static void transmitPendingFrames(const UdpardMicrosecond  time_usec,
                                  const size_t             iface_count,
                                  struct TxPipeline* const tx_pipelines)
{
    for (size_t i = 0; i < iface_count; i++)
    {
        struct TxPipeline* const pipe = &tx_pipelines[i];
        struct UdpardTxItem*     tqi  = udpardTxPeek(&pipe->udpard_tx);  // Find the highest-priority datagram.
        while (tqi != NULL)
        {
            // Attempt transmission only if the frame is not yet timed out while waiting in the TX queue.
            // Otherwise, just drop it and move on to the next one.
            if ((tqi->deadline_usec == 0) || (tqi->deadline_usec > time_usec))
            {
                const int16_t send_res = udpTxSend(&pipe->io,
                                                   tqi->destination.ip_address,
                                                   tqi->destination.udp_port,
                                                   tqi->dscp,
                                                   tqi->datagram_payload.size,
                                                   tqi->datagram_payload.data);
                if (send_res == 0)
                {
                    break;  // Socket no longer writable, stop sending for now to retry later.
                }
                if (send_res < 0)
                {
                    (void) fprintf(stderr, "Iface #%zu send error: %i\n", i, errno);
                }
            }
            udpardTxFree(pipe->udpard_tx.memory, udpardTxPop(&pipe->udpard_tx, tqi));
            tqi = udpardTxPeek(&pipe->udpard_tx);
        }
    }
}

/// This function is invoked from pollRx when a subscription socket becomes readable.
/// It takes ownership of the payload.
/// Returns a negative error code or a non-negative value on success.
static int16_t acceptDatagramForSubscription(const UdpardMicrosecond               timestamp_usec,
                                             const struct UdpardMutablePayload     payload,
                                             const UdpardNodeID                    local_node_id,
                                             const struct ApplicationMemory* const memory,
                                             struct Subscriber* const              sub,
                                             const uint_fast8_t                    iface_index)
{
    int16_t                 out      = 0;
    struct UdpardRxTransfer transfer = {0};
    const int16_t           rx_result =
        (int16_t) udpardRxSubscriptionReceive(&sub->subscription, timestamp_usec, payload, iface_index, &transfer);
    switch (rx_result)
    {
    case 1:
    {
        assert(sub->handler != NULL);
        // Watch out: as we're using the standard Berkeley socket API, if there's a subject that we both subscribe to
        // and publish to at the same time, we will see our own data on the subscription socket.
        // We don't want to process our own data, so we filter out such frames here.
        // Anonymous traffic published by our node will still be accepted though, but this is acceptable.
        // We can't filter based on the IP address because there may be multiple nodes sharing the same IP address
        // (one example is the local loopback interface).
        if ((local_node_id == UDPARD_NODE_ID_UNSET) || (transfer.source_node_id != local_node_id))
        {
            sub->handler(sub, &transfer);
        }
        udpardRxFragmentFree(transfer.payload,  // Free the payload after the transfer is handled.
                             memory->rx.fragment,
                             (struct UdpardMemoryDeleter){
                                 .user_reference = memory->rx.payload.user_reference,
                                 .deallocate     = memory->rx.payload.deallocate,
                             });
        break;
    }
    case 0:
        break;  // No transfer available yet.
    default:
        assert(rx_result == -UDPARD_ERROR_MEMORY);
        out = rx_result;
        break;
    }
    return out;
}

/// Same but for RPC-service datagrams.
static int16_t acceptDatagramForRPC(const UdpardMicrosecond               timestamp_usec,
                                    const struct UdpardMutablePayload     payload,
                                    const struct ApplicationMemory* const memory,
                                    struct RPCDispatcher* const           dispatcher,
                                    const uint_fast8_t                    iface_index,
                                    const size_t                          iface_count,
                                    struct TxPipeline* const              tx)
{
    int16_t                    out      = 0;
    struct UdpardRxRPCTransfer transfer = {0};
    struct UdpardRxRPCPort*    rpc_port = NULL;
    const int16_t rx_res = (int16_t) udpardRxRPCDispatcherReceive(&dispatcher->udpard_rpc_dispatcher,  // //
                                                                  timestamp_usec,
                                                                  payload,
                                                                  iface_index,
                                                                  &rpc_port,
                                                                  &transfer);
    switch (rx_res)
    {
    case 1:
    {
        (void) fprintf(stderr,
                       "RPC request on service %u from client %u with transfer-ID %" PRIu64 " via iface #%u\n",
                       transfer.service_id,
                       transfer.base.source_node_id,
                       transfer.base.transfer_id,
                       iface_index);
        assert(rpc_port != NULL);
        struct RPCServer* const server = (struct RPCServer*) rpc_port;
        assert(server->handler != NULL);
        server->handler(server, &transfer, iface_count, tx);
        udpardRxFragmentFree(transfer.base.payload,  // Free the payload after the transfer is handled.
                             memory->rx.fragment,
                             (struct UdpardMemoryDeleter){
                                 .user_reference = memory->rx.payload.user_reference,
                                 .deallocate     = memory->rx.payload.deallocate,
                             });
        break;
    }
    case 0:
        break;  // No transfer available yet.
    default:
        assert(rx_res == -UDPARD_ERROR_MEMORY);
        out = rx_res;
        break;
    }
    return out;
}

/// Blocks and processes pending frames from the RX sockets of all network interfaces and feeds them into the library;
/// also pushes the frames from the TX queues into their respective sockets.
/// Unblocks either when there is data to handle or when the deadline is reached. May unblock early.
static void doIO(const UdpardMicrosecond unblock_deadline, struct Application* const app)
{
    // Try pushing pending TX frames ahead of time; this is non-blocking.
    // The reason we do it before blocking is that the application may have generated additional frames to transmit.
    const UdpardMicrosecond ts_before_usec = getMonotonicMicroseconds();
    transmitPendingFrames(ts_before_usec, app->iface_count, &app->tx_pipeline[0]);

    // Fill out the TX awaitable array. May be empty if there's nothing to transmit at the moment.
    size_t         tx_count                                     = 0;
    UDPTxAwaitable tx_await[UDPARD_NETWORK_INTERFACE_COUNT_MAX] = {0};
    for (size_t i = 0; i < app->iface_count; i++)
    {
        if (app->tx_pipeline[i].udpard_tx.queue_size > 0)  // There's something to transmit!
        {
            tx_await[tx_count].handle         = &app->tx_pipeline[i].io;
            tx_await[tx_count].user_reference = &app->tx_pipeline[i];
            tx_count++;
        }
    }

    // Fill out the RX awaitable array.
    size_t         rx_count                                          = 0;
    UDPRxAwaitable rx_await[UDPARD_NETWORK_INTERFACE_COUNT_MAX * 10] = {0};
    for (size_t i = 0; i < app->iface_count; i++)  // Subscription sockets (one per topic per iface).
    {
        rx_await[rx_count].handle         = &app->sub_pnp_node_id_allocation.io[i];
        rx_await[rx_count].user_reference = &app->sub_pnp_node_id_allocation;
        rx_count++;
        if (app->sub_data.enabled)
        {
            rx_await[rx_count].handle         = &app->sub_data.io[i];
            rx_await[rx_count].user_reference = &app->sub_data;
            rx_count++;
        }
        assert(rx_count <= (sizeof(rx_await) / sizeof(rx_await[0])));
    }
    if (app->local_node_id <= UDPARD_NODE_ID_MAX)  // The RPC socket is not initialized until the node-ID is set.
    {
        for (size_t i = 0; i < app->iface_count; i++)  // RPC dispatcher sockets (one per iface).
        {
            rx_await[rx_count].handle         = &app->rpc_dispatcher.io[i];
            rx_await[rx_count].user_reference = NULL;
            rx_count++;
            assert(rx_count <= (sizeof(rx_await) / sizeof(rx_await[0])));
        }
    }

    // Block until something happens or the deadline is reached.
    const int16_t wait_result = udpWait((unblock_deadline > ts_before_usec) ? (unblock_deadline - ts_before_usec) : 0,
                                        tx_count,
                                        &tx_await[0],
                                        rx_count,
                                        &rx_await[0]);
    if (wait_result < 0)
    {
        abort();  // Unreachable.
    }

    // Process the RX sockets that became readable.
    // The time has to be re-sampled because the blocking wait may have taken a long time.
    const UdpardMicrosecond ts_after_usec = getMonotonicMicroseconds();
    for (size_t i = 0; i < rx_count; i++)
    {
        if (!rx_await[i].ready)
        {
            continue;  // This one is not yet ready to read.
        }
        // Allocate memory that we will read the data into. The ownership of this memory will be transferred
        // to LibUDPard, which will free it when it is no longer needed.
        // A deeply embedded system may be able to transfer this memory directly from the NIC driver to eliminate copy.
        struct UdpardMutablePayload payload = {
            .size = RX_BUFFER_SIZE,
            .data = app->memory.rx.payload.allocate(app->memory.rx.payload.user_reference, RX_BUFFER_SIZE),
        };
        if (NULL == payload.data)
        {
            (void) fprintf(stderr, "RX payload allocation failure: out of memory\n");
            continue;
        }
        // Read the data from the socket into the buffer we just allocated.
        const int16_t rx_result = udpRxReceive(rx_await[i].handle, &payload.size, payload.data);
        assert(0 != rx_result);
        if (rx_result < 0)
        {
            // We end up here if the socket was closed while processing another datagram.
            // This happens if a subscriber chose to unsubscribe dynamically.
            app->memory.rx.payload.deallocate(app->memory.rx.payload.user_reference, RX_BUFFER_SIZE, payload.data);
            continue;
        }
        // Pass the data buffer into LibUDPard for further processing. It takes ownership of the buffer.
        //
        // We use the user_reference to differentiate subscription sockets from RPC sockets.
        // This is a little hacky but we can't subtype it to add custom state because that breaks the array layout.
        // All of the better solutions I could come up with are various shades of bad:
        //    - Pass awaitables as an array of pointers -- requires an extra array.
        //    - Use a linked list -- results in a clumsy API.
        //    - Add the required field to the awaitable type -- breaks encapsulation.
        if (rx_await[i].user_reference != NULL)
        {
            struct Subscriber* const sub = (struct Subscriber*) rx_await[i].user_reference;
            if (sub->enabled)
            {
                const uint8_t iface_index = (uint8_t) (rx_await[i].handle - &sub->io[0]);
                const int16_t read_result = acceptDatagramForSubscription(ts_after_usec,
                                                                          payload,
                                                                          app->local_node_id,
                                                                          &app->memory,
                                                                          sub,
                                                                          iface_index);
                if (read_result < 0)
                {
                    (void)
                        fprintf(stderr, "Iface #%u RX subscription processing error: %i\n", iface_index, read_result);
                }
            }
            else  // The subscription was disabled while processing other socket reads. Ignore it.
            {
                app->memory.rx.payload.deallocate(app->memory.rx.payload.user_reference, RX_BUFFER_SIZE, payload.data);
            }
        }
        else
        {
            const uint8_t iface_index = (uint8_t) (rx_await[i].handle - &app->rpc_dispatcher.io[0]);
            assert(iface_index < UDPARD_NETWORK_INTERFACE_COUNT_MAX);
            const int16_t read_result = acceptDatagramForRPC(ts_after_usec,
                                                             payload,
                                                             &app->memory,
                                                             &app->rpc_dispatcher,
                                                             iface_index,
                                                             app->iface_count,
                                                             &app->tx_pipeline[0]);
            if (read_result < 0)
            {
                (void) fprintf(stderr, "Iface #%u RX RPC processing error: %i\n", iface_index, read_result);
            }
        }
    }

    // While processing the RX data we may have generated additional outgoing frames.
    // Plus we may have pending frames from before the blocking call.
    transmitPendingFrames(ts_after_usec, app->iface_count, &app->tx_pipeline[0]);
}

/// Returns a register view exposing the detailed memory allocation statistics to remote nodes.
/// This is useful for diagnostics. Naturally, the application can define an arbitrary number of such registers
/// for advanced diagnostics, and even for direct low-level state mutation if needed (this may be useful during
/// development or for advanced maintenance). For example, it could expose performance counters in this way.
static uavcan_register_Value_1_0 getRegisterSysInfoMem(struct Register* const self)
{
    const struct ApplicationMemory* const mem = self->user_reference;
    assert(mem != NULL);
    uavcan_register_Value_1_0 out = {0};
    uavcan_register_Value_1_0_select_natural64_(&out);
    uavcan_primitive_array_Natural64_1_0* const val   = &out.natural64;
    const struct MemoryBlockAllocator*          mba[] = {mem->rx.session.user_reference,
                                                         mem->rx.fragment.user_reference,
                                                         mem->rx.payload.user_reference};
    for (size_t i = 0; i < (sizeof(mba) / sizeof(mba[0])); i++)
    {
        val->value.elements[val->value.count++] = mba[i]->block_count;
        val->value.elements[val->value.count++] = mba[i]->block_size_bytes;
        val->value.elements[val->value.count++] = mba[i]->used_blocks_peak;
        val->value.elements[val->value.count++] = mba[i]->request_count;
        val->value.elements[val->value.count++] = mba[i]->oom_count;
    }
    return out;
}

/// A helper for registering registers of a given port.
static void regInitPort(struct PortRegisterSet* const self,
                        struct Register** const       root,
                        const char* const             prefix,
                        const char* const             port_name,
                        const char* const             port_type)
{
    assert((self != NULL) && (root != NULL) && (port_name != NULL) && (port_type != NULL));
    (void) memset(self, 0, sizeof(*self));

    registerInit(&self->id, root, (const char*[]){"uavcan", prefix, port_name, "id", NULL});
    uavcan_register_Value_1_0_select_natural16_(&self->id.value);
    self->id.value.natural16.value.count       = 1;
    self->id.value.natural16.value.elements[0] = UINT16_MAX;
    self->id.persistent                        = true;
    self->id.remote_mutable                    = true;

    registerInit(&self->type, root, (const char*[]){"uavcan", prefix, port_name, "type", NULL});
    uavcan_register_Value_1_0_select_string_(&self->type.value);
    self->type.value._string.value.count = strlen(port_type);
    (void) memcpy(&self->type.value._string.value.elements[0], port_type, self->type.value._string.value.count);
    self->type.persistent = true;
}

/// A helper for registering registers of a given publisher port.
static void regInitPublisher(struct PublisherRegisterSet* const self,
                             struct Register** const            root,
                             const char* const                  port_name,
                             const char* const                  port_type)
{
    assert((self != NULL) && (root != NULL) && (port_name != NULL) && (port_type != NULL));
    (void) memset(self, 0, sizeof(*self));
    regInitPort(&self->base, root, "pub", port_name, port_type);

    registerInit(&self->priority, root, (const char*[]){"uavcan", "pub", port_name, "prio", NULL});
    uavcan_register_Value_1_0_select_natural8_(&self->priority.value);
    self->priority.value.natural8.value.count       = 1;
    self->priority.value.natural8.value.elements[0] = UdpardPriorityNominal;
    self->priority.persistent                       = true;
    self->priority.remote_mutable                   = true;
}

/// A helper for registering registers of a given subscriber port.
static void regInitSubscriber(struct SubscriberRegisterSet* const self,
                              struct Register** const             root,
                              const char* const                   port_name,
                              const char* const                   port_type)
{
    assert((self != NULL) && (root != NULL) && (port_name != NULL) && (port_type != NULL));
    (void) memset(self, 0, sizeof(*self));
    regInitPort(&self->base, root, "sub", port_name, port_type);
}

/// Initializes all registers with their default values.
/// The next step after this is to load the values from the non-volatile storage,
/// thus overriding the defaults with user-configured parameters.
static void initRegisters(struct ApplicationRegisters* const reg,
                          struct ApplicationMemory* const    mem,
                          struct Register** const            root)
{
    // The standard node-ID register.
    registerInit(&reg->node_id, root, (const char*[]){"uavcan", "node", "id", NULL});
    uavcan_register_Value_1_0_select_natural16_(&reg->node_id.value);
    reg->node_id.value.natural16.value.count       = 1;
    reg->node_id.value.natural16.value.elements[0] = UDPARD_NODE_ID_UNSET;
    reg->node_id.persistent                        = true;
    reg->node_id.remote_mutable                    = true;

    // The standard description register; can be mutated by the integrator arbitrarily.
    registerInit(&reg->node_description, root, (const char*[]){"uavcan", "node", "description", NULL});
    uavcan_register_Value_1_0_select_string_(&reg->node_description.value);  // Empty by default.
    reg->node_description.persistent     = true;
    reg->node_description.remote_mutable = true;

    // The standard interface list register. Defaults to the loopback interface.
    registerInit(&reg->udp_iface, root, (const char*[]){"uavcan", "udp", "iface", NULL});
    uavcan_register_Value_1_0_select_string_(&reg->udp_iface.value);
    reg->udp_iface.persistent                = true;
    reg->udp_iface.remote_mutable            = true;
    reg->udp_iface.value._string.value.count = strlen(DEFAULT_IFACE);
    (void) memcpy(&reg->udp_iface.value._string.value.elements[0],
                  DEFAULT_IFACE,
                  reg->udp_iface.value._string.value.count);

    // The standard DSCP mapping register. The recommended DSCP mapping is all zeros; see the Cyphal/UDP specification.
    // Refer to RFC 2474 and RFC 8837 for more information on DSCP.
    registerInit(&reg->udp_dscp, root, (const char*[]){"uavcan", "udp", "dscp", NULL});
    uavcan_register_Value_1_0_select_natural8_(&reg->udp_dscp.value);
    reg->udp_dscp.persistent                 = true;
    reg->udp_dscp.remote_mutable             = true;
    reg->udp_dscp.value.natural8.value.count = UDPARD_PRIORITY_MAX + 1;
    (void) memset(&reg->udp_dscp.value.natural8.value.elements[0], 0, reg->udp_dscp.value.natural8.value.count);

    // An application-specific register exposing the memory usage of the application.
    registerInit(&reg->mem_info, root, (const char*[]){"sys", "info", "mem", NULL});
    reg->mem_info.getter         = &getRegisterSysInfoMem;
    reg->mem_info.user_reference = mem;

    // Publisher port registers.
    regInitPublisher(&reg->pub_data, root, "my_data", uavcan_primitive_array_Real32_1_0_FULL_NAME_AND_VERSION_);

    // Subscriber port registers.
    regInitSubscriber(&reg->sub_data, root, "my_data", uavcan_primitive_array_Real32_1_0_FULL_NAME_AND_VERSION_);
}

/// This is designed for use with registerTraverse.
/// The context points to a size_t containing the number of registers loaded.
static void* regLoad(struct Register* const self, void* const context)
{
    assert((self != NULL) && (context != NULL));
    byte_t serialized[uavcan_register_Value_1_0_EXTENT_BYTES_];
    size_t sr_size = uavcan_register_Value_1_0_EXTENT_BYTES_;
    // Ignore non-persistent registers and those whose values are computed dynamically (can't be stored).
    // If the entry is not found or the stored value is invalid, the default value will be used.
    if (self->persistent && (self->getter == NULL) && storageGet(self->name, &sr_size, &serialized[0]) &&
        (uavcan_register_Value_1_0_deserialize_(&self->value, &serialized[0], &sr_size) >= 0))
    {
        ++(*(size_t*) context);
    }
    return NULL;
}

/// This is designed for use with registerTraverse.
/// The context points to a size_t containing the number of registers that could not be stored.
static void* regStore(struct Register* const self, void* const context)
{
    assert((self != NULL) && (context != NULL));
    if (self->persistent && self->remote_mutable)
    {
        byte_t     serialized[uavcan_register_Value_1_0_EXTENT_BYTES_];
        size_t     sr_size = uavcan_register_Value_1_0_EXTENT_BYTES_;
        const bool ok      = (uavcan_register_Value_1_0_serialize_(&self->value, serialized, &sr_size) >= 0) &&
                        storagePut(self->name, sr_size, &serialized[0]);
        if (!ok)
        {
            ++(*(size_t*) context);
        }
    }
    return NULL;
}

/// This is designed for use with registerTraverse.
/// The context is not used.
static void* regReset(struct Register* const self, void* const context)
{
    (void) context;
    assert(self != NULL);
    if (self->persistent && self->remote_mutable)
    {
        (void) storageDrop(self->name);
    }
    return NULL;
}

/// Parse the addresses of the available local network interfaces from the given string.
/// In a deeply embedded system this may be replaced by some other networking APIs, like LwIP.
/// Invalid interface addresses are ignored; i.e., this is a best-effort parser.
/// Returns the number of valid ifaces found (which may be zero).
static uint_fast8_t parseNetworkIfaceAddresses(const uavcan_primitive_String_1_0* const in,
                                               uint32_t out[UDPARD_NETWORK_INTERFACE_COUNT_MAX])
{
    uint_fast8_t count = 0;
    char         buf_z[uavcan_primitive_String_1_0_value_ARRAY_CAPACITY_ + 1];
    size_t       offset = 0;
    assert(in->value.count <= sizeof(buf_z));
    while ((offset < in->value.count) && (count < UDPARD_NETWORK_INTERFACE_COUNT_MAX))
    {
        // Copy chars from "in" into "buf_z" one by one until a whitespace character is found.
        size_t sz = 0;
        while ((offset < in->value.count) && (sz < (sizeof(buf_z) - 1)))
        {
            const char c = (char) in->value.elements[offset++];
            if ((c == ' ') || (c == '\t') || (c == '\r') || (c == '\n'))
            {
                break;
            }
            buf_z[sz++] = c;
        }
        buf_z[sz] = '\0';
        if (sz > 0)
        {
            const uint32_t iface = udpParseIfaceAddress(buf_z);
            if (iface > 0)
            {
                out[count++] = iface;
            }
        }
    }
    return count;
}

/// This is needed to implement node restarting. Remove this if running on an embedded system.
extern char** environ;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

int main(const int argc, char* const argv[])
{
    // The block size values used here are derived from the sizes of the structs defined in LibUDPard and the MTU.
    // They may change when migrating between different versions of the library or when building the code for a
    // different platform, so it may be desirable to choose conservative values here (i.e. larger than necessary).
    // Small MCUs may have smaller sizeof(void*) and sizeof(size_t) (e.g., on AVR these are 2 bytes only),
    // which makes these sizes quite a bit smaller, too.
    MEMORY_BLOCK_ALLOCATOR_DEFINE(mem_session, 384, RESOURCE_LIMIT_SESSIONS);
    MEMORY_BLOCK_ALLOCATOR_DEFINE(mem_fragment, 88, RESOURCE_LIMIT_PAYLOAD_FRAGMENTS);
    MEMORY_BLOCK_ALLOCATOR_DEFINE(mem_payload, 2048, RESOURCE_LIMIT_PAYLOAD_FRAGMENTS);

    // Set up the god application object.
    struct Application app = {
        .memory        = {.rx =
                              {
                                  .session  = {.user_reference = &mem_session,
                                               .allocate       = &memoryBlockAllocate,
                                               .deallocate     = &memoryBlockDeallocate},
                                  .fragment = {.user_reference = &mem_fragment,
                                               .allocate       = &memoryBlockAllocate,
                                               .deallocate     = &memoryBlockDeallocate},
                                  .payload  = {.user_reference = &mem_payload,
                                               .allocate       = &memoryBlockAllocate,
                                               .deallocate     = &memoryBlockDeallocate},
                       },
                          .tx =
                              {
                                  .fragment = {.user_reference = &mem_fragment,
                                               .allocate       = &memoryBlockAllocate,
                                               .deallocate     = &memoryBlockDeallocate},
                                  .payload  = {.user_reference = &mem_payload,
                                               .allocate       = &memoryBlockAllocate,
                                               .deallocate     = &memoryBlockDeallocate},
                       }},
        .iface_count   = 0,
        .local_node_id = UDPARD_NODE_ID_UNSET,
    };
    getUniqueID(&app.unique_id[0]);

    // The first thing to do during the application initialization is to load the register values from the non-volatile
    // configuration storage. Non-volatile configuration is essential for most Cyphal nodes because it contains
    // information on how to reach the network and how to publish/subscribe to the subjects of interest.
    initRegisters(&app.reg, &app.memory, &app.reg_root);
    {
        size_t load_count = 0;
        (void) registerTraverse(app.reg_root, &regLoad, &load_count);
        (void) fprintf(stderr, "%zu registers loaded from the non-volatile storage\n", load_count);
    }
    // If we're running on a POSIX system, we can use the environment variables to override the loaded values.
    // There is a standard mapping between environment variable names and register names documented in the DSDL
    // definition of uavcan.register.Access; for example, "uavcan.node.id" --> "UAVCAN__NODE__ID".
    // This simple feature is left as an exercise to the reader. It is meaningless in a deeply embedded system though.
    //
    //  registerTraverse(app.reg, &regOverrideFromEnvironmentVariables, NULL);

    // Parse the iface addresses given via the standard iface register.
    app.iface_count = parseNetworkIfaceAddresses(&app.reg.udp_iface.value._string, &app.ifaces[0]);
    if (app.iface_count == 0)  // In case of error we fall back to the local loopback to keep the node reachable.
    {  // This can be replaced with any other default value that makes sense for the application.
        // A deeply embedded system may perform DHCP allocation instead and use the dynamically allocated IP addresses
        // if no addresses are given explicitly.
        (void) fprintf(stderr, "Using the loopback iface because the iface register does not specify valid ifaces\n");
        app.iface_count = 1;
        app.ifaces[0]   = udpParseIfaceAddress(DEFAULT_IFACE);
        assert(app.ifaces[0] > 0);
    }

    // Initialize the TX pipelines. We have one per local iface (unlike the RX pipelines which are shared).
    for (size_t i = 0; i < app.iface_count; i++)
    {
        if ((0 != udpardTxInit(&app.tx_pipeline[i].udpard_tx, &app.local_node_id, TX_QUEUE_SIZE, app.memory.tx)) ||
            (0 != udpTxInit(&app.tx_pipeline[i].io, app.ifaces[i])))
        {
            (void) fprintf(stderr, "Failed to initialize TX pipeline for iface %zu\n", i);
            return 1;
        }
        for (size_t k = 0; k <= UDPARD_PRIORITY_MAX; k++)
        {
            app.tx_pipeline[i].udpard_tx.dscp_value_per_priority[k] = app.reg.udp_dscp.value.natural8.value.elements[k];
        }
    }

    // Initialize the local node-ID. The register value may change at runtime; we don't want the change to take
    // effect until the node is restarted, so we initialize the local node-ID only once at startup.
    app.local_node_id = app.reg.node_id.value.natural16.value.elements[0];

    // Initialize the publishers. They are not dependent on the local node-ID value.
    initPublisher(&app.pub_heartbeat, UdpardPriorityNominal, uavcan_node_Heartbeat_1_0_FIXED_PORT_ID_, 1 * MEGA);
    initPublisher(&app.pub_port_list, UdpardPriorityOptional, uavcan_node_port_List_1_0_FIXED_PORT_ID_, 10 * MEGA);
    initPublisher(&app.pub_pnp_node_id_allocation,
                  UdpardPrioritySlow,
                  uavcan_pnp_NodeIDAllocationData_2_0_FIXED_PORT_ID_,
                  1 * MEGA);
    initPublisher(&app.pub_data,
                  app.reg.pub_data.priority.value.natural8.value.elements[0],
                  app.reg.pub_data.base.id.value.natural16.value.elements[0],
                  50 * KILO);

    // Initialize the subscribers. They are not dependent on the local node-ID value.
    const struct UdpardRxMemoryResources rx_memory = {
        .session  = app.memory.rx.session,
        .fragment = app.memory.rx.fragment,
        .payload  = {.user_reference = app.memory.rx.payload.user_reference,
                     .deallocate     = app.memory.rx.payload.deallocate},
    };
    {
        const int16_t res = initSubscriber(&app.sub_pnp_node_id_allocation,
                                           uavcan_pnp_NodeIDAllocationData_2_0_FIXED_PORT_ID_,
                                           uavcan_pnp_NodeIDAllocationData_2_0_EXTENT_BYTES_,
                                           &cbOnNodeIDAllocationData,
                                           rx_memory,
                                           app.iface_count,
                                           &app.ifaces[0]);
        if (res < 0)
        {
            (void) fprintf(stderr, "Failed to subscribe to uavcan.pnp.NodeIDAllocationData.2: %i\n", res);
            return 1;
        }
        assert(app.sub_pnp_node_id_allocation.enabled);
        app.sub_pnp_node_id_allocation.user_reference = &app;
    }
    {
        const int16_t res = initSubscriber(&app.sub_data,
                                           app.reg.sub_data.base.id.value.natural16.value.elements[0],
                                           uavcan_primitive_array_Real32_1_0_EXTENT_BYTES_,
                                           &cbOnMyData,
                                           rx_memory,
                                           app.iface_count,
                                           &app.ifaces[0]);
        if (res < 0)
        {
            (void) fprintf(stderr, "Failed to subscribe to my_data: %i\n", res);
            return 1;
        }
        app.sub_data.user_reference = &app;
    }

    // Initialize the RPC services. First, we initialize the dispatcher.
    // If the local node-ID is already known, we can start the dispatcher right away;
    // otherwise, the starting part will be postponed until the PnP node-ID allocation is finished.
    if (udpardRxRPCDispatcherInit(&app.rpc_dispatcher.udpard_rpc_dispatcher, rx_memory) != 0)
    {
        abort();  // This is infallible.
    }
    if (app.local_node_id <= UDPARD_NODE_ID_MAX)  // Start if node-ID is known, otherwise wait until it is known.
    {
        const int16_t rpc_start_res = startRPCDispatcher(&app.rpc_dispatcher,  //
                                                         app.local_node_id,
                                                         app.iface_count,
                                                         &app.ifaces[0]);
        if (rpc_start_res < 0)
        {
            (void) fprintf(stderr, "RPC dispatcher start failed: %i\n", rpc_start_res);
            return 1;
        }
    }
    // Initialize the RPC server ports.
    if (initRPCServer(&app.srv_get_node_info,
                      &app.rpc_dispatcher.udpard_rpc_dispatcher,
                      uavcan_node_GetInfo_1_0_FIXED_PORT_ID_,
                      uavcan_node_GetInfo_Request_1_0_EXTENT_BYTES_,
                      &cbOnGetNodeInfoRequest) != 1)
    {
        abort();
    }
    app.srv_get_node_info.user_reference = &app;
    //
    if (initRPCServer(&app.srv_execute_command,
                      &app.rpc_dispatcher.udpard_rpc_dispatcher,
                      uavcan_node_ExecuteCommand_1_1_FIXED_PORT_ID_,
                      uavcan_node_ExecuteCommand_Request_1_1_EXTENT_BYTES_,
                      &cbOnExecuteCommandRequest) != 1)
    {
        abort();
    }
    app.srv_execute_command.user_reference = &app;
    //
    if (initRPCServer(&app.srv_register_list,
                      &app.rpc_dispatcher.udpard_rpc_dispatcher,
                      uavcan_register_List_1_0_FIXED_PORT_ID_,
                      uavcan_register_List_Request_1_0_EXTENT_BYTES_,
                      &cbOnRegisterListRequest) != 1)
    {
        abort();
    }
    app.srv_register_list.user_reference = app.reg_root;  // Cannot add new registers after this.
    //
    if (initRPCServer(&app.srv_register_access,
                      &app.rpc_dispatcher.udpard_rpc_dispatcher,
                      uavcan_register_Access_1_0_FIXED_PORT_ID_,
                      uavcan_register_Access_Request_1_0_EXTENT_BYTES_,
                      &cbOnRegisterAccessRequest) != 1)
    {
        abort();
    }
    app.srv_register_access.user_reference = app.reg_root;  // Cannot add new registers after this.

    // RUN THE MAIN LOOP.
    (void) fprintf(stderr, "NODE STARTED\n");
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
        // Run socket I/O. It will block until network activity or until the specified deadline (may unblock sooner).
        doIO(next_1_hz_iter_at, &app);
    }

    // Save registers immediately before restarting the node.
    // We don't access the storage during normal operation of the node because access is slow and is impossible to
    // perform without blocking; it also introduces undesirable complexities and complicates the failure modes.
    if (!app.factory_reset_required)
    {
        size_t store_errors = 0;
        (void) registerTraverse(app.reg_root, &regStore, &store_errors);
        if (store_errors > 0)
        {
            (void) fprintf(stderr, "%zu registers could not be stored\n", store_errors);
        }
    }
    else
    {
        (void) registerTraverse(app.reg_root, &regReset, NULL);
    }

    // It is recommended to postpone restart until all frames are sent though.
    (void) argc;
    (void) fprintf(stderr, "\nRESTART\n");
    return -execve(argv[0], argv, environ);
}
