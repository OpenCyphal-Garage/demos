# Find nnvg and setup python environment to generate C++ from DSDL.
# Copyright 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
# Copyright 2021 UAVCAN Consortium <consortium@uavcan.org>
# This file is adapted from: https://github.com/UAVCAN/nunavut/blob/main/verification/cmake/modules/Findnnvg.cmake

# Creates a target that will generate source code from dsdl definitions.
#
# Extra command line arguments can be passed to nnvg by setting the string variable NNVG_FLAGS.
#
# :param str ARG_TARGET_NAME:               The name to give the target.
# :param str ARG_OUTPUT_LANGUAGE            The language to generate for this target.
# :param Path ARG_OUTPUT_FOLDER:            The directory to generate all source under.
# :param Path ARG_DSDL_ROOT_DIR:            A directory containing the root namespace dsdl.
# :param bool ARG_ENABLE_OVR_VAR_ARRAY:     Generates code with variable array capacity override enabled
# :param str ARG_SER_ENDIANNESS:            One of 'any', 'big', or 'little' to pass as the value of the
#                                           nnvg `--target-endianness` argument. Set to an empty string
#                                           to omit this argument.
# :param str ARG_GENERATE_SUPPORT:          Value for the nnvg --generate-support argument. Valid values are:
#                                           as-needed (default) - generate support code if serialization is enabled.
#                                           always - always generate support code.
#                                           never - never generate support code.
#                                           only - only generate support code.
# :param ...:                               A list of paths to use when looking up dependent DSDL types.
# :returns: Sets a variable "ARG_TARGET_NAME"-OUTPUT in the parent scope to the list of files the target
#           will generate. For example, if ARG_TARGET_NAME == 'foo-bar' then after calling this function
#           ${foo-bar-OUTPUT} will be set to the list of output files.
function(create_dsdl_target ARG_TARGET_NAME
        ARG_OUTPUT_LANGUAGE
        ARG_OUTPUT_FOLDER
        ARG_DSDL_ROOT_DIR
        ARG_ENABLE_OVR_VAR_ARRAY
        ARG_SER_ENDIANNESS
        ARG_GENERATE_SUPPORT)

    separate_arguments(NNVG_CMD_ARGS UNIX_COMMAND "${NNVG_FLAGS}")

    if (${ARGC} GREATER 7)
        math(EXPR ARG_N_LAST "${ARGC}-1")
        foreach (ARG_N RANGE 7 ${ARG_N_LAST})
            list(APPEND NNVG_CMD_ARGS "-I")
            list(APPEND NNVG_CMD_ARGS "${ARGV${ARG_N}}")
        endforeach (ARG_N)
    endif ()

    list(APPEND NNVG_CMD_ARGS --target-language)
    list(APPEND NNVG_CMD_ARGS ${ARG_OUTPUT_LANGUAGE})
    list(APPEND NNVG_CMD_ARGS -O)
    list(APPEND NNVG_CMD_ARGS ${ARG_OUTPUT_FOLDER})
    list(APPEND NNVG_CMD_ARGS ${ARG_DSDL_ROOT_DIR})
    list(APPEND NNVG_CMD_ARGS "--target-endianness")
    list(APPEND NNVG_CMD_ARGS ${ARG_SER_ENDIANNESS})
    list(APPEND NNVG_CMD_ARGS "--enable-serialization-asserts")
    list(APPEND NNVG_CMD_ARGS "--generate-support")
    list(APPEND NNVG_CMD_ARGS ${ARG_GENERATE_SUPPORT})

    if (ARG_ENABLE_OVR_VAR_ARRAY)
        list(APPEND NNVG_CMD_ARGS "--enable-override-variable-array-capacity")
        message(STATUS "Enabling variable array capacity override option in generated code.")
    endif ()

    execute_process(COMMAND ${NNVG} --list-outputs ${NNVG_CMD_ARGS}
            OUTPUT_VARIABLE OUTPUT_FILES
            RESULT_VARIABLE LIST_OUTPUTS_RESULT)

    if (NOT LIST_OUTPUTS_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to retrieve a list of headers nnvg would "
                "generate for the ${ARG_TARGET_NAME} target (${LIST_OUTPUTS_RESULT})"
                " (${NNVG})")
    endif ()

    execute_process(COMMAND ${NNVG} --list-inputs ${NNVG_CMD_ARGS}
            OUTPUT_VARIABLE INPUT_FILES
            RESULT_VARIABLE LIST_INPUTS_RESULT)

    if (NOT LIST_INPUTS_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to resolve inputs using nnvg for the ${ARG_TARGET_NAME} "
                "target (${LIST_INPUTS_RESULT})"
                " (${NNVG})")
    endif ()

    add_custom_command(OUTPUT ${OUTPUT_FILES}
            COMMAND ${NNVG} ${NNVG_CMD_ARGS}
            DEPENDS ${INPUT_FILES}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Running nnvg ${NNVG_CMD_ARGS}")

    add_custom_target(${ARG_TARGET_NAME}-gen
            DEPENDS ${OUTPUT_FILES})

    add_library(${ARG_TARGET_NAME} INTERFACE)

    add_dependencies(${ARG_TARGET_NAME} ${ARG_TARGET_NAME}-gen)

    target_include_directories(${ARG_TARGET_NAME} INTERFACE ${ARG_OUTPUT_FOLDER})

    set(${ARG_TARGET_NAME}-OUTPUT ${OUTPUT_FILES} PARENT_SCOPE)

endfunction(create_dsdl_target)


find_program(NNVG nnvg)

if (NNVG)
    execute_process(COMMAND ${NNVG} --version OUTPUT_VARIABLE NNVG_VERSION RESULT_VARIABLE NNVG_VERSION_RESULT)
    if (NNVG_VERSION_RESULT EQUAL 0)
        string(STRIP ${NNVG_VERSION} NNVG_VERSION)
        message(STATUS "${NNVG} --version: ${NNVG_VERSION}")
    endif ()
endif ()

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(nnvg REQUIRED_VARS NNVG_VERSION)
