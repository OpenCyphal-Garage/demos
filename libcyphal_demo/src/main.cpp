/// This software is distributed under the terms of the MIT License.
/// Copyright (C) 2021 OpenCyphal <consortium@opencyphal.org>
/// Author: Sergei Shirokov <sergei.shirokov@zubax.com>

#include "can/socket_can_media.hpp"
#include "multiplexer.hpp"

#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/transport/can/media.hpp>
#include <libcyphal/transport/can/transport.hpp>
#include <libcyphal/transport/errors.hpp>
#include <libcyphal/transport/msg_sessions.hpp>
#include <libcyphal/types.hpp>
#include <uavcan/node/Heartbeat_1_0.h>

#include <array>
#include <cstddef>
#include <iostream>
#include <utility>

int main(int, char**)
{
    const std::size_t tx_capacity{16};

    Multiplexer multiplexer;
    auto&       mr = *cetl::pmr::new_delete_resource();

    auto can_media = can::SocketCanMedia::make("vcan0", false);

    std::array<libcyphal::transport::can::IMedia*, 1> media = {can_media.get()};

    auto maybe_transport = libcyphal::transport::can::makeTransport(mr, multiplexer, media, tx_capacity, cetl::nullopt);
    if (nullptr != cetl::get_if<libcyphal::transport::FactoryError>(&maybe_transport))
    {
        std::cerr << "Failed to create transport.\n";
        return 1;
    }
    auto transport =
        std::move(*cetl::get_if<libcyphal::UniquePtr<libcyphal::transport::can::ICanTransport>>(&maybe_transport));

    auto maybe_heartbeat_session = transport->makeMessageTxSession({uavcan_node_Heartbeat_1_0_FIXED_PORT_ID_});
    if (nullptr != cetl::get_if<libcyphal::transport::AnyError>(&maybe_heartbeat_session))
    {
        std::cerr << "Failed to create 'Heartbeat' msg tx session.\n";
        return 1;
    }
    auto heartbeat_session =
        std::move(*cetl::get_if<libcyphal::UniquePtr<libcyphal::transport::IMessageTxSession>>(&maybe_heartbeat_session));

    transport->run(libcyphal::MonotonicClock::now());
    heartbeat_session->run(libcyphal::MonotonicClock::now());

    std::cout << "Hello, World!\n";
    return 0;
}