# ==========================================================================
# Copyright (C) 2022 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==========================================================================

# Build system for middle-layer layer, that includes:
# - abstractions used on both hardware and software path, such as:
#   - wrappers around analytic operations, CRC, etc. (analytics/, other/)
#   - Huffman Table object implementation and (de)compression flow (compression/)
#   - allocators (common/)
#   - etc.
# - logic related to submission to the accelerator (dispatcher/, util/)

enable_language(CXX)

file(GLOB MIDDLE_LAYER_SRC
        analytics/*.cpp
        c_wrapper/*.cpp
        checksum/*.cpp
        common/*.cpp
        compression/*.cpp
        compression/*/*.cpp
        compression/*/*/*.cpp
        dispatcher/*.cpp
        other/*.cpp
        util/*.cpp
        inflate/*.cpp
        ../core-iaa/sources/accelerator/*.cpp) # todo

add_library(middle_layer_lib OBJECT
        ${MIDDLE_LAYER_SRC})

set_property(GLOBAL APPEND PROPERTY QPL_LIB_DEPS
             $<TARGET_OBJECTS:middle_layer_lib>)

if (WIN32)
    modify_standard_language_flag(LANGUAGE_NAME "CXX"
            FLAG_NAME "/GR"
            NEW_FLAG_VALUE "-")
endif ()

target_compile_options(middle_layer_lib
        PRIVATE $<$<C_COMPILER_ID:GNU>:${QPL_LINUX_TOOLCHAIN_REQUIRED_FLAGS};
                                       ${QPL_LINUX_TOOLCHAIN_DYNAMIC_LIBRARY_FLAGS};
                                       $<$<CONFIG:Release>:-O3;-D_FORTIFY_SOURCE=2>>
        PRIVATE $<$<COMPILE_LANG_AND_ID:CXX,GNU>:${QPL_LINUX_TOOLCHAIN_CPP_EMBEDDED_FLAGS}>
        PRIVATE $<$<C_COMPILER_ID:MSVC>:${QPL_WINDOWS_TOOLCHAIN_REQUIRED_FLAGS};
                                        ${QPL_WINDOWS_TOOLCHAIN_DYNAMIC_LIBRARY_FLAGS}
                                        $<$<CONFIG:Release>:-O2>>
        PRIVATE $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:${QPL_WINDOWS_TOOLCHAIN_CPP_EMBEDDED_FLAGS}>)

target_compile_definitions(middle_layer_lib
        PUBLIC QPL_VERSION="${CMAKE_PROJECT_VERSION}"
        PUBLIC $<$<C_COMPILER_ID:MSVC>:_ENABLE_EXTENDED_ALIGNED_STORAGE>
        PUBLIC $<$<BOOL:${LOG_HW_INIT}>:LOG_HW_INIT>
        PUBLIC $<$<BOOL:${EFFICIENT_WAIT}>:QPL_EFFICIENT_WAIT>
        PUBLIC QPL_BADARG_CHECK
        PUBLIC $<$<BOOL:${DYNAMIC_LOADING_LIBACCEL_CONFIG}>:DYNAMIC_LOADING_LIBACCEL_CONFIG>)

set_target_properties(middle_layer_lib PROPERTIES CXX_STANDARD 17)

target_include_directories(middle_layer_lib
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        PUBLIC $<TARGET_PROPERTY:qpl,INTERFACE_INCLUDE_DIRECTORIES>
        PUBLIC $<TARGET_PROPERTY:qplcore_sw_dispatcher,INTERFACE_INCLUDE_DIRECTORIES>
        PUBLIC $<TARGET_PROPERTY:isal,INTERFACE_INCLUDE_DIRECTORIES>
        PUBLIC $<TARGET_PROPERTY:core_iaa,INTERFACE_INCLUDE_DIRECTORIES>)

target_compile_definitions(middle_layer_lib PUBLIC -DQPL_LIB)
