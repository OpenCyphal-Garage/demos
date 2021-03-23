# This software is distributed under the terms of the MIT License.
# Copyright (C) 2021 UAVCAN Consortium <consortium@uavcan.org>
# Author: Pavel Kirienko <pavel@uavcan.org>

cmake_minimum_required(VERSION 3.16)

if (NOT DEFINED DS015_SERVO_DEMO_PLATFORM OR DS015_SERVO_DEMO_PLATFORM STREQUAL "linux")
    set(DS015_SERVO_DEMO_PLATFORM "linux")
    set(LOCAL_BUILD_APPLICATION ON)
    message(STATUS "Setting DS015_SERVO_DEMO_PLATFORM = ${DS015_SERVO_DEMO_PLATFORM}")
else()
    message(STATUS "Using ${DS015_SERVO_DEMO_PLATFORM} for DS015_SERVO_DEMO_PLATFORM")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/cmake/toolchains/${DS015_SERVO_DEMO_PLATFORM}.cmake" CACHE PATH "toolchain file")
    set(LOCAL_BUILD_APPLICATION OFF)
    set(LOCAL_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/platform/${DS015_SERVO_DEMO_PLATFORM}/src/flash.ld)
endif()


project(ds015_servo_demo C CXX)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")

set(submodules "${CMAKE_SOURCE_DIR}/../submodules")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -pedantic -fstrict-aliasing")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wdouble-promotion -Wswitch-enum -Wfloat-equal -Wundef")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wconversion -Wtype-limits")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wsign-conversion -Wcast-align -Wmissing-declarations")

# Forward the revision information to the compiler so that we could expose it at runtime. This is entirely optional.
execute_process(
        COMMAND git rev-parse --short=8 HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/.."
        OUTPUT_VARIABLE vcs_revision_id
        OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(STATUS "vcs_revision_id: ${vcs_revision_id}")
add_definitions(
        -DVERSION_MAJOR=1
        -DVERSION_MINOR=0
        -DVCS_REVISION_ID=0x${vcs_revision_id}ULL
        -DNODE_NAME="org.uavcan.demos.ds015.servo"
)

# Transpile DSDL into C using Nunavut. Install Nunavut as follows: pip install nunavut.
# Alternatively, you can invoke the transpiler manually or use https://nunaweb.uavcan.org.
find_package(nnvg REQUIRED)
set(dsdl_root_namespace_dirs                # List all DSDL root namespaces to transpile here.
        ${submodules}/public_regulated_data_types/uavcan
        ${submodules}/public_regulated_data_types/reg
)

# Generate the Nunavut support headers for c code.
create_dsdl_target(
        "nunavut_support"
        c
        ${CMAKE_BINARY_DIR}/transpiled
        ""
        OFF
        little
        "only"
)

foreach(ns_dir ${dsdl_root_namespace_dirs})
    get_filename_component(ns ${ns_dir} NAME)
    message(STATUS "DSDL namespace ${ns} at ${ns_dir}")
    create_dsdl_target(
            "dsdl_${ns}"                    # CMake target name
            c                               # Target language to transpile into
            ${CMAKE_BINARY_DIR}/transpiled  # Destination directory (add it to the includes)
            ${ns_dir}                       # Source directory
            OFF                             # Disable variable array capacity override
            little                          # Endianness of the target platform (alternatives: "big", "any")
            "never"                         # Support files arg genearated once in the nunavut-support target (above)
            ${dsdl_root_namespace_dirs}     # Look-up DSDL namespaces
    )
endforeach()
include_directories(SYSTEM ${CMAKE_BINARY_DIR}/transpiled)  # Make the transpiled headers available for inclusion.
add_definitions(-DNUNAVUT_ASSERT=assert)

# Build libcanard.
add_library(canard STATIC ${submodules}/libcanard/libcanard/canard.c)
include_directories(SYSTEM ${submodules}/libcanard/libcanard)

# Build o1heap -- a hard real-time deterministic memory allocator for embedded systems.
add_library(o1heap STATIC ${submodules}/o1heap/o1heap/o1heap.c)
include_directories(SYSTEM ${submodules}/o1heap/o1heap/)

# Build the platform layer
file(GLOB LOCAL_PLATFORM_LAYER_SOURCE
     LIST_DIRECTORIES false
     RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
     platform/${DS015_SERVO_DEMO_PLATFORM}/src/*.c
)

add_library(platform STATIC ${LOCAL_PLATFORM_LAYER_SOURCE})
include_directories(SYSTEM platform/${DS015_SERVO_DEMO_PLATFORM}/include/
                           platform/)

add_dependencies(platform dsdl_uavcan dsdl_reg nunavut_support)

if (LOCAL_BUILD_APPLICATION)

# +---------------------------------------------------------------------------+
# | BUILD REGULAR APPLICATION
# +---------------------------------------------------------------------------+
    message(STATUS "Build ${CMAKE_PROJECT_NAME} applicaiton for platform \"${DS015_SERVO_DEMO_PLATFORM}\"")

    # Build the application.
    add_executable(${CMAKE_PROJECT_NAME}
            src/main.c
    )

    add_dependencies(${CMAKE_PROJECT_NAME} dsdl_uavcan dsdl_reg nunavut_support)
    target_link_libraries(${CMAKE_PROJECT_NAME} canard o1heap platform)

else()
# +---------------------------------------------------------------------------+
# | BUILD FIRMWARE BINARIES
# +---------------------------------------------------------------------------+
    message(STATUS "Build ds015_servo_demo binary for platform \"${DS015_SERVO_DEMO_PLATFORM}\"")

    add_executable(${CMAKE_PROJECT_NAME}.elf
                   src/main.c)

    set_target_properties(${CMAKE_PROJECT_NAME}.elf
                          PROPERTIES
                            LINK_DEPENDS ${LOCAL_LINKER_SCRIPT})

    target_link_options(${CMAKE_PROJECT_NAME}.elf BEFORE
                        PUBLIC
                            "-T" "${LOCAL_LINKER_SCRIPT}"
    )

    target_link_libraries(${CMAKE_PROJECT_NAME}.elf canard o1heap platform)


    add_custom_target(${CMAKE_PROJECT_NAME}.hex ALL
                      COMMAND
                        ${CMAKE_OBJCOPY} -O ihex ${CMAKE_PROJECT_NAME}.elf ${PROJECT_NAME}.hex
                      COMMENT
                          "Generting Intel Hex file from ${CMAKE_PROJECT_NAME}.elf"
    )

    add_dependencies(${CMAKE_PROJECT_NAME}.hex ${CMAKE_PROJECT_NAME}.elf)
    
    add_custom_target(${CMAKE_PROJECT_NAME}.bin ALL
                      COMMAND
                        ${CMAKE_OBJCOPY} -O binary ${CMAKE_PROJECT_NAME}.elf ${PROJECT_NAME}.bin
                      COMMENT
                        "Generting binary file from ${CMAKE_PROJECT_NAME}.elf"
    )

endif()
