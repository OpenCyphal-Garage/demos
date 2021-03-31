# DS-015 servo demo

## Purpose

This demo implements the full [DS-015](https://github.com/Dronecode/SIG-CAN-Drone)
servo network service in a highly portable C application that can be trivially
adapted to run in a baremetal environment.
Unless ported, the demo is intended for evaluation on GNU/Linux.

This demo supports only UAVCAN/CAN at the moment, but it can be extended to support UAVCAN/UDP or UAVCAN/serial.

The servo network service is defined for two kinds of actuators: translational and rotary.
The only difference is that one uses `reg.drone.physics.dynamics.translation.Linear`,
and the other uses `reg.drone.physics.dynamics.rotation.Planar`.
The types can be replaced if necessary.

DS-015 comes with a hard requirement that a node shall be equipped with non-volatile memory for keeping the
node registers (that is, configuration parameters).
It is not possible to construct a compliant implementation without non-volatile memory.


## Preparation

You will need GNU/Linux, CMake, a C11 compiler, [Yakut](https://github.com/UAVCAN/yakut),
and [SocketCAN utils](https://github.com/linux-can/can-utils).

Build the demo as follows:

```bash
git clone --recursive https://github.com/UAVCAN/demos
cd demos/ds015_servo
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
./ds015_servo_demo
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

A practical system would always assign static node-ID instead of relying on this behavior to ensure
deterministic behaviors at startup.
This, however, cannot be done until we have a node-ID allocated so that we are able to configure the node via UAVCAN.
Therefore, we launch a PnP node-ID allocator available in Yakut (PX4 also implements one):

```bash
export UAVCAN__CAN__IFACE="socketcan:vcan0"
export UAVCAN__NODE__ID=127                 # This node-ID is for Yakut.
yakut monitor -P ~/allocation_table.db
```

This command will run the monitor together with the allocator.
You will see our node get itself a node-ID allocated,
then roughly the following picture should appear on the monitor:

<img src="docs/monitor-initial.png" alt="yakut monitor">

That means that our node is running,
but it is unable to perform any servo-related activities because the respective subjects remain unconfigured.
Configure them (do not stop the monitor though, otherwise you won't know what's happening on the bus),
assuming that the node got allocated the node-ID of 125:

```bash
export UAVCAN__CAN__IFACE="socketcan:vcan0"
export UAVCAN__NODE__ID=126                 # This node-ID is for Yakut.
yakut call 125 uavcan.register.Access.1.0 "{name: {name: uavcan.sub.servo.readiness.id}, value: {natural16: {value: 10}}}"
yakut call 125 uavcan.register.Access.1.0 "{name: {name: uavcan.sub.servo.setpoint.id},  value: {natural16: {value: 50}}}"
yakut call 125 uavcan.register.Access.1.0 "{name: {name: uavcan.pub.servo.dynamics.id},  value: {natural16: {value: 100}}}"
yakut call 125 uavcan.register.Access.1.0 "{name: {name: uavcan.pub.servo.feedback.id},  value: {natural16: {value: 101}}}"
yakut call 125 uavcan.register.Access.1.0 "{name: {name: uavcan.pub.servo.power.id},     value: {natural16: {value: 102}}}"
yakut call 125 uavcan.register.Access.1.0 "{name: {name: uavcan.pub.servo.status.id},    value: {natural16: {value: 103}}}"
```

The node is configured now, but we need to restart it before the configuration parameter changes take effect:

```bash
yakut call 125 uavcan.node.ExecuteCommand.1.1 "command: 65535"
```

You should see candump start printing a lot more frames (approx. 150 per second).
The demo should still print `DISARMED` in the terminal.
Let's arm it and publish some setpoint:

```bash
yakut pub --period=0.5 --count=30 \
    10:reg.drone.service.common.Readiness.0.1 'value: 3' \
    50:reg.drone.physics.dynamics.translation.Linear.0.1 'kinematics: {position: {meter: -3.14}}'
```

You will see the message that is printed on the terminal change from `DISARMED`
to the current setpoint values.
The monitor should show you something close to this:

<img src="docs/monitor.png" alt="yakut monitor">

Shortly after the publisher is stopped the servo will automatically disarm itself,
as dictated by the DS-015 standard.

You can listen for the dynamics subject published by the node as follows:

```bash
export UAVCAN__CAN__IFACE="socketcan:vcan0"
yakut sub 100:reg.drone.physics.dynamics.translation.LinearTs.0.1
```

You can erase the configuration and go back to factory defaults as follows:

```bash
export UAVCAN__CAN__IFACE="socketcan:vcan0"
export UAVCAN__NODE__ID=126                 # This node-ID is for Yakut.
yakut call 125 uavcan.node.ExecuteCommand.1.1 "command: 65532"
```


## Porting

Just read the code.

The files `socketcan.[ch]` were taken from <https://github.com/UAVCAN/platform_specific_components>.
You may (or may not) find something relevant for your target platform there, too.
