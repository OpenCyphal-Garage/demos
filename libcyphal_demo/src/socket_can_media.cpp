/// This software is distributed under the terms of the MIT License.
/// Copyright (C) 2021 OpenCyphal <consortium@opencyphal.org>
/// Author: Sergei Shirokov <sergei.shirokov@zubax.com>

#include "socket_can_media.hpp"

#include <libcyphal/transport/can/media.hpp>
#include <socketcan.h>

#include <unistd.h>

void xxx()
{
    auto socket_can_fd = ::socketcanOpen("vcan0", false);
    ::close(socket_can_fd);
}
