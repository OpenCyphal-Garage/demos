# Setup arm-none-eabi-gcc toolchain to build the S32K146 firmware
# Copyright 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
# Copyright 2021 UAVCAN Consortium <consortium@uavcan.org>

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(LOCAL_TOOLCHAIN arm-none-eabi)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

string(CONCAT LOCAL_TOOLCHAIN_GEN_ARGS  "-ggdb "
                                        "-O0 "
                                        "-mcpu=cortex-m7 "
                                        "-march=armv7e-m+fp "
                                        "-mfloat-abi=hard "
                                        "-mthumb "
                                        "-mno-thumb-interwork "
                                        "-ffunction-sections "
                                        "-fdata-sections ")

set(CMAKE_EXE_LINKER_FLAGS "${LOCAL_TOOLCHAIN_GEN_ARGS} --specs=nosys.specs -Wl,--gc-sections -Wl,--print-memory-usage -Wl,-Map,${CMAKE_PROJECT_NAME}.map" CACHE INTERNAL "Linker options")

set(CMAKE_C_FLAGS "${LOCAL_TOOLCHAIN_GEN_ARGS} -ffunction-sections -fdata-sections")


#---------------------------------------------------------------------------------------
# Set compilers
#---------------------------------------------------------------------------------------
set(CMAKE_C_COMPILER ${LOCAL_TOOLCHAIN}-gcc CACHE INTERNAL "C Compiler")
set(CMAKE_CXX_COMPILER ${LOCAL_TOOLCHAIN}-g++ CACHE INTERNAL "C++ Compiler")
set(CMAKE_ASM_COMPILER ${LOCAL_TOOLCHAIN}-gcc CACHE INTERNAL "ASM Compiler")
set(CMAKE_OBJCOPY ${LOCAL_TOOLCHAIN}-objcopy CACHE INTERNAL "Objcopy Utility")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

add_definitions(-DCPU_S32K146)
