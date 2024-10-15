# LibCyphal demo application

This demo application is a usage demonstrator for [LibCyphal](https://github.com/OpenCyphal-Garage/libcyphal) ---
a compact Cyphal UDP and CAN implementation for high-integrity systems written in C++14 and above.
It implements a simple Cyphal node that showcases the following features:

- Fixed port-ID and non-fixed port-ID publishers.
- Fixed port-ID and non-fixed port-ID subscribers.
- Fixed port-ID RPC server.
- Plug-and-play node-ID allocation unless it is configured statically.
- Fast Cyphal Register API and non-volatile storage for the persistent registers.
- Support for redundant network interfaces.

This document will walk you through the process of building, running, and evaluating the demo
on a GNU/Linux-based OS.
It can be easily ported to another platform, such as a baremetal MCU,
by replacing the POSIX socket API and stdio with suitable alternatives;
for details, please consult with `udp.h` and `storage.h`. **FIXME**

## Preparation

You will need GNU/Linux, CMake, a C++14 compiler.
Install:
- [Yakut](https://github.com/OpenCyphal/yakut) CLI tool,
- For CAN transport [SocketCAN utils](https://github.com/linux-can/can-utils)
- For UDP transport Wireshark with the [Cyphal plugins](https://github.com/OpenCyphal/wireshark_plugins)

Build the demo:

```shell
git clone --recursive https://github.com/OpenCyphal/demos
cd demos/libcyphal_demo
mkdir build && cd build
cmake .. && make
```