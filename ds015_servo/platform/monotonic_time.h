///                         __   __   _______   __   __   _______   _______   __   __
///                        |  | |  | /   _   ` |  | |  | /   ____| /   _   ` |  ` |  |
///                        |  | |  | |  |_|  | |  | |  | |  |      |  |_|  | |   `|  |
///                        |  |_|  | |   _   | `  `_/  / |  |____  |   _   | |  |`   |
///                        `_______/ |__| |__|  `_____/  `_______| |__| |__| |__| `__|
///                            |      |            |         |      |         |
///                        ----o------o------------o---------o------o---------o-------
///
/// Provides monotonic time from the platform. This is needed to support the demo firmware and to provide monotonic time
/// for the required UAVCAN Heartbeat message
///
/// This software is distributed under the terms of the MIT License.
/// Copyright (C) 2021 UAVCAN Consortium <consortium@uavcan.org>
/// Author: Pavel Kirienko <pavel@uavcan.org>

#ifndef MONOTONIC_TIME_H_INCLUDED
#define MONOTONIC_TIME_H_INCLUDED

#include "canard.h"

/// A deeply embedded system should sample a microsecond-resolution non-overflowing 64-bit timer.
/// Here is a simple non-blocking implementation as an example:
/// https://github.com/PX4/sapog/blob/601f4580b71c3c4da65cc52237e62a/firmware/src/motor/realtime/motor_timer.c#L233-L274
/// Mind the difference between monotonic time and wall time. Monotonic time never changes rate or makes leaps,
/// it is therefore impossible to synchronize with an external reference. Wall time can be synchronized and therefore
/// it may change rate or make leap adjustments. The two kinds of time serve completely different purposes.
CanardMicrosecond getMonotonicMicroseconds();

#endif  // MONOTONIC_TIME_H_INCLUDED
