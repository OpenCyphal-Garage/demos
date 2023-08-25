///                            ____                   ______            __          __
///                           / __ `____  ___  ____  / ____/_  ______  / /_  ____  / /
///                          / / / / __ `/ _ `/ __ `/ /   / / / / __ `/ __ `/ __ `/ /
///                         / /_/ / /_/ /  __/ / / / /___/ /_/ / /_/ / / / / /_/ / /
///                         `____/ .___/`___/_/ /_/`____/`__, / .___/_/ /_/`__,_/_/
///                             /_/                     /____/_/
///
/// This module implements non-volatile storage for this application. On an embedded system this may be backed
/// by a raw memory chip or a compact fault-tolerant file system like LittleFS. High-integrity embedded systems
/// may benefit from not accessing the storage memory during normal operation at all; the recommended approach is
/// to read the configuration from the storage memory once during the boot-up and then keep it in RAM;
/// if new configuration needs to be stored, it is to be done immediately before the reboot.
///
/// This software is distributed under the terms of the MIT License.
/// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
/// Copyright Amazon.com Inc. or its affiliates.
/// SPDX-License-Identifier: MIT
/// Author: Pavel Kirienko <pavel@opencyphal.org>

#pragma once

#include <stdbool.h>
#include <stdlib.h>

/// Returns true on success, false if there is no such key, I/O error, or bad parameters.
/// At entry, inout_size contains the size of the output buffer; upon return it contains the number of bytes read.
bool storageGet(const char* const key, size_t* const inout_size, void* const data);

/// Existing key will be overwritten. If there is no such key, it will be created.
/// This function should normally be only called before the reboot when configuration changes need to be committed
/// to the non-volatile storage. Optionally it may be invoked during the boot-up, if required by the application
/// logic, but never during normal operation.
/// Returns true on success, false on I/O error or bad parameters.
bool storagePut(const char* const key, const size_t size, const void* const data);

/// Removes the key from the storage. Does nothing if the key does not exist.
/// This is useful when the configuration needs to be reset to the default values, or during version migration.
/// Returns true on success, false on I/O error or bad parameters.
bool storageDrop(const char* const key);
