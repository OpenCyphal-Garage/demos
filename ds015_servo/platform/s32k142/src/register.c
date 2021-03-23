/// This software is distributed under the terms of the MIT License.
/// Copyright (C) 2021 UAVCAN Consortium <consortium@uavcan.org>
/// Author: Pavel Kirienko <pavel@uavcan.org>

#include "register.h"

void registerRead(const char* const register_name, uavcan_register_Value_1_0* const inout_value)
{
    (void)register_name;
    (void)inout_value;
}

void registerWrite(const char* const register_name, const uavcan_register_Value_1_0* const value)
{
    (void)register_name;
    (void)value;
}

uavcan_register_Name_1_0 registerGetNameByIndex(const uint16_t index)
{
    (void)index;
    uavcan_register_Name_1_0 dummy;
    dummy.name.count = 0;
    return dummy;
}

bool registerAssign(uavcan_register_Value_1_0* const dst, const uavcan_register_Value_1_0* const src)
{
    (void)dst;
    (void)src;
    return false;
}

void registerDoFactoryReset(void)
{

}
