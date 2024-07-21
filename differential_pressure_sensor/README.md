# Differential pressure sensor demo

## Purpose

This demo implements a simple differential pressure & static air temperature sensor node
in a highly portable C application that can be trivially adapted to run in a baremetal environment.
Unless ported, the demo is intended for evaluation on GNU/Linux.

This demo supports only Cyphal/CAN at the moment, but it can be extended to support Cyphal/UDP or Cyphal/serial.


## Preparation

You will need GNU/Linux, CMake, a C11 compiler, [Yakut](https://github.com/OpenCyphal/yakut),
and [SocketCAN utils](https://github.com/linux-can/can-utils).

Build the demo as follows 
(the default builds for can-fd, you can pass `-DCAN_CLASSIC=y` to cmake to build for can classic):

```bash
git clone --recursive https://github.com/OpenCyphal/demos
cd demos/differential_pressure_sensor
mkdir build && cd build
cmake .. && make
```


## Running

Set up a virtual CAN bus `vcan0`:

```bash
modprobe can
modprobe can_raw
modprobe vcan
ip link add dev vcan0 type vcan
ip link set vcan0 mtu 72         # Enable CAN FD by configuring the MTU of 64+8
ip link set up vcan0
```

Launch the node
(it is built to emulate an embedded system so it does not accept any arguments or environment variables):

```bash
./differential_pressure_sensor
```

It may print a few informational messages and then go silent.

Fire up the CAN dump utility from SocketCAN utils and see what's happening on the bus.
You should see the PnP node-ID allocation requests being sent by our node irregularly:

```bash
$ candump -decaxta vcan0
(1616445708.288978)  vcan0  TX B -  197FE510  [20]  FF FF C6 69 73 51 FF 4A EC 29 CD BA AB F2 FB E3 46 7C 00 E9
(1616445711.289044)  vcan0  TX B -  197FE510  [20]  FF FF C6 69 73 51 FF 4A EC 29 CD BA AB F2 FB E3 46 7C 00 EA
# and so on...
```

It will keep doing this forever until it got an allocation response from the node-ID allocator.

Next, we launch a PnP node-ID allocator available in Yakut (PX4 also implements one):

```bash
export UAVCAN__CAN__IFACE="socketcan:vcan0"
export UAVCAN__NODE__ID=127                     # This node-ID is for Yakut.
y mon --plug-and-play ~/allocation_table.db
```

This command will run the monitor together with the allocator.
You will see our node get itself a node-ID allocated,
then roughly the following picture should appear on the monitor:

<img src="docs/monitor-initial.png" alt="yakut monitor">

That means that our node is running, but it is unable to publish measurements because the respective subjects
remain unconfigured.
So let's configure them (do not stop the monitor though, otherwise you won't know what's happening on the bus),
assuming that the node got allocated the node-ID of 125.
First, it helps to know what registers are available at all:

```bash
$ export UAVCAN__CAN__IFACE="socketcan:vcan0"
$ export UAVCAN__NODE__ID=126                   # This node-ID is for Yakut.
$ y rl 125
[reg.udral.service.pitot, uavcan.can.mtu, uavcan.node.description, uavcan.node.id, uavcan.node.unique_id, uavcan.pub.airspeed.differential_pressure.id, uavcan.pub.airspeed.differential_pressure.type, uavcan.pub.airspeed.static_air_temperature.id, uavcan.pub.airspeed.static_air_temperature.type, udral.pnp.cookie]
$ y rl 125, | y rb                              # You can also read all registers like this
# (output not shown)
```

Configure the subject-IDs:

```bash
y r 125 uavcan.pub.airspeed.differential_pressure.id  100
y r 125 uavcan.pub.airspeed.static_air_temperature.id 101
```

The node is configured now, but we need to restart it before the configuration parameter changes take effect:

```bash
y cmd 125 restart -e
```

You should see candump start printing a lot more frames because the demo is now publishing the sensor data.
The monitor will also show the subjects that we just configured.

<img src="docs/monitor.png" alt="yakut monitor">

You can subscribe to the published differential pressure using Yakut as follows:

```bash
y sub 100:uavcan.si.unit.pressure.scalar
```

You can erase the configuration and go back to factory defaults as follows:

```bash
y cmd 125 factory_reset
```


## Porting

Just read the code.

The files `socketcan.[ch]` were taken from <https://github.com/OpenCyphal/platform_specific_components>.
You may (or may not) find something relevant for your target platform there, too.
