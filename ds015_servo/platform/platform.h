///                         __   __   _______   __   __   _______   _______   __   __
///                        |  | |  | /   _   ` |  | |  | /   ____| /   _   ` |  ` |  |
///                        |  | |  | |  |_|  | |  | |  | |  |      |  |_|  | |   `|  |
///                        |  |_|  | |   _   | `  `_/  / |  |____  |   _   | |  |`   |
///                        `_______/ |__| |__|  `_____/  `_______| |__| |__| |__| `__|
///                            |      |            |         |      |         |
///                        ----o------o------------o---------o------o---------o-------
///
/// Init and share time with the platform layer of this demo.
///
/// This software is distributed under the terms of the MIT License.
/// Copyright (C) 2021 UAVCAN Consortium <consortium@uavcan.org>
/// Author: Pavel Kirienko <pavel@uavcan.org>

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <o1heap.h>

/// Initialize the platform layer and return a heap allocator.
/// Returns 0 on success or non-zero on error
int platformInit(O1HeapInstance** out_allocator);

/// Call a 1Hz to give the platform layer some time for things like
/// kicking watchdog timers.
void platformService(void);
