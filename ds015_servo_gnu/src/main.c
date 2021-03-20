// This software is distributed under the terms of the MIT License.
// Copyright (C) 2021 UAVCAN Consortium <consortium@uavcan.org>
// Author: Pavel Kirienko <pavel@uavcan.org>

#include "socketcan.h"
#include <canard.h>
#include <o1heap.h>
#include <uavcan/node/Heartbeat_1_0.h>
#include <uavcan/_register/Access_1_0.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <stdbool.h>

/// Store the given register value into the persistent storage.
/// In this demo, we use the filesystem for persistence. A real embedded application would typically use some
/// non-volatile memory for the same purpose (e.g., direct writes into the on-chip EEPROM);
/// more complex embedded systems might leverage a fault-tolerant embedded filesystem like LittleFS.
static void registerSet(const char* const register_name, const uavcan_register_Value_1_0* const value)
{
    uint8_t      serialized[uavcan_register_Value_1_0_EXTENT_BYTES_] = {0};
    size_t       sr_size                                             = uavcan_register_Value_1_0_EXTENT_BYTES_;
    const int8_t err = uavcan_register_Value_1_0_serialize_(value, serialized, &sr_size);
    if (err >= 0)
    {
        FILE* const fp = fopen(&register_name[0], "wb");
        if (fp != NULL)
        {
            (void) fwrite(&serialized[0], 1U, sr_size, fp);
            (void) fclose(fp);
        }
    }
}

/// Reads the specified register from the persistent storage into `inout_value`. If the register does not exist,
/// the default will not be modified but stored into the persistent storage using @ref registerSet().
/// If the value exists in the persistent storage but it is of a different type, it is treated as non-existent,
/// unless the provided value is empty.
static void registerGet(const char* const register_name, uavcan_register_Value_1_0* const inout_default)
{
    assert(inout_default != NULL);
    FILE* fp = fopen(&register_name[0], "rb");
    if (fp == NULL)
    {
        registerSet(register_name, inout_default);
        fp = fopen(&register_name[0], "rb");
    }
    if (fp != NULL)
    {
        uint8_t serialized[uavcan_register_Value_1_0_EXTENT_BYTES_] = {0};
        size_t  sr_size = fread(&serialized[0], 1U, uavcan_register_Value_1_0_EXTENT_BYTES_, fp);
        (void) fclose(fp);
        uavcan_register_Value_1_0 out = {0};
        const int8_t              err = uavcan_register_Value_1_0_deserialize_(&out, serialized, &sr_size);
        if (err >= 0)
        {
            const bool same_type = (inout_default->_tag_ == out._tag_) ||  //
                                   uavcan_register_Value_1_0_is_empty_(inout_default);
            if (same_type)  // Otherwise, consider non-existent. The correct type will be enforced at next write.
            {
                *inout_default = out;
            }
        }
    }
}

typedef enum SubjectRole
{
    SUBJECT_ROLE_PUBLISHER,
    SUBJECT_ROLE_SUBSCRIBER,
} SubjectRole;

/// Reads the port-ID from the corresponding standard register. The standard register schema is documented in
/// the UAVCAN Specification, section for the standard service uavcan.register.Access. You can also find it here:
/// https://github.com/UAVCAN/public_regulated_data_types/blob/master/uavcan/register/384.Access.1.0.uavcan
/// A very hands-on demo is available in Python: https://pyuavcan.readthedocs.io/en/stable/pages/demo.html
static CanardPortID getSubjectID(const SubjectRole role, const char* const port_name, const char* const type_name)
{
    // Deduce the register name from port name.
    const char* const role_name = (role == SUBJECT_ROLE_PUBLISHER) ? "pub" : "sub";
    char              register_name[uavcan_register_Name_1_0_name_ARRAY_CAPACITY_] = {0};
    snprintf(&register_name[0], sizeof(register_name), "uavcan.%s.%s.id", role_name, port_name);

    // Set up the default value. It will be used to populate the register if it doesn't exist.
    uavcan_register_Value_1_0 val = {0};
    uavcan_register_Value_1_0_select_natural16_(&val);
    val.natural16.value.count       = 1;
    val.natural16.value.elements[0] = UINT16_MAX;  // This means "undefined", per Specification, which is the default.

    // Read the register with defensive self-checks.
    registerGet(&register_name[0], &val);
    assert(uavcan_register_Value_1_0_is_natural16_(&val) && (val.natural16.value.count == 1));
    const uint16_t result = val.natural16.value.elements[0];

    // This part is NOT required but recommended by the Specification for enhanced introspection capabilities. It is
    // very cheap to implement so all implementations should do so. This register simply contains the name of the
    // type exposed at this port. It should be immutable but it is not strictly required so in this implementation
    // we take shortcuts by making it mutable since it's behaviorally simpler in this specific case.
    snprintf(&register_name[0], sizeof(register_name), "uavcan.%s.%s.type", role_name, port_name);
    uavcan_register_Value_1_0_select_string_(&val);
    val._string.value.count = nunavutChooseMin(strlen(type_name), uavcan_primitive_String_1_0_value_ARRAY_CAPACITY_);
    memcpy(&val._string.value.elements[0], type_name, val._string.value.count);
    registerSet(&register_name[0], &val);  // Unconditionally overwrite existing value because it's read-only.

    return result;
}

void* canardAllocate(CanardInstance* const ins, const size_t amount)
{
    assert(o1heapDoInvariantsHold((O1HeapInstance*) (ins->user_reference)));
    return o1heapAllocate((O1HeapInstance*) (ins->user_reference), amount);
}

void canardFree(CanardInstance* const ins, void* const pointer)
{
    o1heapFree((O1HeapInstance*) (ins->user_reference), pointer);
}

int main()
{
    // A simple application like a servo node typically does not require more than 16 KiB of heap.
    // For the background and related theory refer to the following resources:
    // - https://github.com/UAVCAN/libcanard/blob/master/README.md
    // - https://github.com/pavel-kirienko/o1heap/blob/master/README.md
    // - https://forum.uavcan.org/t/uavcanv1-libcanard-nunavut-templates-memory-usage-concerns/1118/4?u=pavel.kirienko
    _Alignas(O1HEAP_ALIGNMENT) static uint8_t heap_arena[1024 * 16] = {0};

    // If you are using an RTOS or another multithreaded environment, pass critical section enter/leave functions
    // in the last two arguments instead of NULL.
    O1HeapInstance* const heap = o1heapInit(heap_arena, sizeof(heap_arena), NULL, NULL);
    if (heap == NULL)
    {
        return 1;
    }

    // The libcanard instance requires the allocator for managing protocol states.
    CanardInstance canard = canardInit(&canardAllocate, &canardFree);
    canard.user_reference = heap;

    // Restore the node-ID from the corresponding standard register. Default to anonymous.
    uavcan_register_Value_1_0 val = {0};
    uavcan_register_Value_1_0_select_natural16_(&val);
    val.natural16.value.count       = 1;
    val.natural16.value.elements[0] = UINT16_MAX;  // This means undefined (anonymous), per Specification/libcanard.
    registerGet("uavcan.node.id", &val);  // The names of the standard registers are regulated by the Specification.
    assert(uavcan_register_Value_1_0_is_natural16_(&val) && (val.natural16.value.count == 1));
    canard.node_id = (val.natural16.value.elements[0] > CANARD_NODE_ID_MAX)
                         ? CANARD_NODE_ID_UNSET
                         : (CanardNodeID) val.natural16.value.elements[0];

    // The description register is optional but recommended because it helps constructing/maintaining large networks.
    // It simply keeps a human-readable description of the node that should be empty by default.
    uavcan_register_Value_1_0_select_string_(&val);
    val._string.value.count = 0;
    registerGet("uavcan.node.description", &val);  // We don't need the value, we just need to ensure it exists.

    // Configure the transport by reading the appropriate standard registers.
    uavcan_register_Value_1_0_select_natural16_(&val);
    val.natural16.value.count = 1;
    val.natural16.value.elements[0] = CANARD_MTU_CAN_FD;
    assert(uavcan_register_Value_1_0_is_natural16_(&val) && (val.natural16.value.count == 1));
    canard.mtu_bytes = val.natural16.value.elements[0];
    // We also need the bitrate configuration register. In this demo we can't really use it but an embedded application
    // shall define "uavcan.can.bitrate" of type natural32[2]; the second value is zero/ignored if CAN FD not supported.
    const int socketcan_fd = socketcanOpen("vcan0", canard.mtu_bytes > CANARD_MTU_CAN_CLASSIC);
    if (socketcan_fd < 0)
    {
        return -socketcan_fd;
    }

    // Now the node is initialized and we're ready to roll.
    // TODO

    return 0;
}
