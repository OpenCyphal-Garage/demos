/// This software is distributed under the terms of the MIT License.
/// Copyright (c) 2020 UAVCAN Development Team.
/// Authors: Pavel Kirienko <pavel.kirienko@zubax.com>, Tom De Rybel <tom.derybel@robocow.be>


#include "socketcan.h"

SocketCANFD socketcanOpen(const char* const iface_name, const bool can_fd)
{
    (void)iface_name;
    (void)can_fd;
    return 0;
}

int16_t socketcanPush(const SocketCANFD fd, const CanardFrame* const frame, const CanardMicrosecond timeout_usec)
{
    (void)fd;
    (void)frame;
    (void)timeout_usec;
    return 0;
}

int16_t socketcanPop(const SocketCANFD       fd,
                     CanardFrame* const      out_frame,
                     const size_t            payload_buffer_size,
                     void* const             payload_buffer,
                     const CanardMicrosecond timeout_usec,
                     bool* const             loopback)
{
    (void)fd;
    (void)out_frame;
    (void)payload_buffer_size;
    (void)payload_buffer;
    (void)timeout_usec;
    (void)loopback;
    return 0;
}

int16_t socketcanFilter(const SocketCANFD fd, const size_t num_configs, const SocketCANFilterConfig* const configs)
{
    (void)fd;
    (void)num_configs;
    (void)configs;
    return 0;
}
