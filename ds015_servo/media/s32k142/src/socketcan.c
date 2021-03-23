/// This software is distributed under the terms of the MIT License.
/// Copyright (c) 2020 UAVCAN Development Team.
/// Authors: Pavel Kirienko <pavel.kirienko@zubax.com>, Tom De Rybel <tom.derybel@robocow.be>


#include "socketcan.h"

SocketCANFD socketcanOpen(const char* const iface_name, const bool can_fd)
{
    return 0;
}

int16_t socketcanPush(const SocketCANFD fd, const CanardFrame* const frame, const CanardMicrosecond timeout_usec)
{
    return 0;
}

int16_t socketcanPop(const SocketCANFD       fd,
                     CanardFrame* const      out_frame,
                     const size_t            payload_buffer_size,
                     void* const             payload_buffer,
                     const CanardMicrosecond timeout_usec,
                     bool* const             loopback)
{
    return 0;
}

int16_t socketcanFilter(const SocketCANFD fd, const size_t num_configs, const SocketCANFilterConfig* const configs)
{
    return 0;
}
