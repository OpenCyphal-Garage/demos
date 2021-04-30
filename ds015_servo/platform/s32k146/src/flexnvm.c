/// This software is distributed under the terms of the MIT License.
/// Copyright (C) 2021 UAVCAN Consortium <consortium@uavcan.org>

#include "register.h"

void registerRead(const char* const register_name, uavcan_register_Value_1_0* const inout_value)
{
    (void) register_name;
    (void) inout_value;
}

void registerWrite(const char* const register_name, const uavcan_register_Value_1_0* const value)
{
    uint8_t      serialized[uavcan_register_Value_1_0_EXTENT_BYTES_] = {0};
    size_t       sr_size                                             = uavcan_register_Value_1_0_EXTENT_BYTES_;
    const int8_t err = uavcan_register_Value_1_0_serialize_(value, serialized, &sr_size);
    (void) err;
    (void) register_name;
}

uavcan_register_Name_1_0 registerGetNameByIndex(const uint16_t index)
{
    uavcan_register_Name_1_0 out = {0};
    uavcan_register_Name_1_0_initialize_(&out);
    (void) index;
    return out;
}

bool registerAssign(uavcan_register_Value_1_0* const dst, const uavcan_register_Value_1_0* const src)
{
    (void) dst;
    (void) src;
    return false;
}

void registerDoFactoryReset(void) {}
