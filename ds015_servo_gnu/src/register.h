///                         __   __   _______   __   __   _______   _______   __   __
///                        |  | |  | /   _   ` |  | |  | /   ____| /   _   ` |  ` |  |
///                        |  | |  | |  |_|  | |  | |  | |  |      |  |_|  | |   `|  |
///                        |  |_|  | |   _   | `  `_/  / |  |____  |   _   | |  |`   |
///                        `_______/ |__| |__|  `_____/  `_______| |__| |__| |__| `__|
///                            |      |            |         |      |         |
///                        ----o------o------------o---------o------o---------o-------
///
/// Registers are named values that keep various configuration parameters of the local UAVCAN node (application).
/// Some of these parameters are used by the business logic of the application (e.g., PID gains, perfcounters);
/// others are used by the UAVCAN stack (e.g., port-IDs, node-ID, transport configuration, introspection, and so on).
/// Registers of the latter category are all named with the same prefix "uavcan.", and their names and semantics
/// are regulated by the Specification to ensure consistency across the ecosystem.
///
/// The Specification doesn't define how the registers are to be stored since this part does not affect network
/// interoperability. In this demo we use a very simple and portable approach where each register is stored as
/// a separate file in the local filesystem; the name of the file matches the name of the register, and the register
/// values are serialized in the DSDL format (i.e., same format that is used for network exchange).
/// Deeply embedded systems may either use the same approach with the help of some compact fault-tolerant filesystem
/// (such as, for example, LittleFS: https://github.com/littlefs-project/littlefs), or they can resort to a low-level
/// specialized approach using on-chip EEPROM or similar.
///
/// This software is distributed under the terms of the MIT License.
/// Copyright (C) 2021 UAVCAN Consortium <consortium@uavcan.org>
/// Author: Pavel Kirienko <pavel@uavcan.org>

#pragma once

#include <uavcan/_register/Name_1_0.h>
#include <uavcan/_register/Value_1_0.h>
#include <stdbool.h>
#include <stdint.h>

// NOTE: this implementation currently does not differentiate between mutable/immutable registers and does not support
// volatile registers. It is trivial to extend though.

/// Reads the specified register from the persistent storage into `inout_value`.
/// If the register does not exist or it cannot be automatically converted to the type of the provided argument,
/// the value will be stored in the persistent storage using @ref registerWrite(), overriding existing value.
/// The default will not be initialized if the argument is empty.
void registerRead(const char* const register_name, uavcan_register_Value_1_0* const inout_value);

/// Store the given register value into the persistent storage.
void registerWrite(const char* const register_name, const uavcan_register_Value_1_0* const value);

/// This function is mostly intended for implementing the standard RPC-service uavcan.register.List.
/// It returns the name of the register at the specified index (where the ordering is undefined but guaranteed
/// to be short-term stable), or empty name if the index is out of bounds.
uavcan_register_Name_1_0 registerGetNameByIndex(const uint16_t index);

/// Copy one value to the other if their types and dimensionality are the same or automatic conversion is possible.
/// If the destination is empty, it is simply replaced with the source (assignment always succeeds).
/// The return value is true if the assignment has been performed, false if it is not possible
/// (in the latter case the destination is NOT modified).
bool registerAssign(uavcan_register_Value_1_0* const dst, const uavcan_register_Value_1_0* const src);

/// Erase all registers such that the defaults are used at the next launch.
void registerDoFactoryReset(void);
