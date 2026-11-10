cmake_minimum_required(VERSION 3.13)

# WP_erase builds a RAM-only program that erases the entire flash of a WP, then pops back up
# as a BOOTSEL USB drive.
# It is step 1 of the 3-step drag-and-drop WP install: erase, partition table, WP image.
# See GETTING_STARTED.md.
#
# The program is the SDK's 'flash_nuke' example. We do not copy its source: like WP's
# use of the ws2812 PIO file, we build it in place from the pico-examples checkout.
# The examples' own CMake cannot be used standalone, so this small wrapper does the job.

if(NOT DEFINED PICO_SDK_PATH)
    message(FATAL_ERROR "PICO_SDK_PATH is undefined: Can't find PicoSDK")
endif()

if(NOT DEFINED PICO_EXAMPLES_PATH)
    message(FATAL_ERROR "PICO_EXAMPLES_PATH is undefined: Can't find pico-examples")
endif()

set(NUKE_SOURCE ${PICO_EXAMPLES_PATH}/flash/nuke/nuke.c)
if(NOT EXISTS ${NUKE_SOURCE})
    message(FATAL_ERROR "Can't find ${NUKE_SOURCE}: is pico-examples checked out at ${PICO_EXAMPLES_PATH}?")
endif()

set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")

# Must be set before pico_sdk_import.cmake
set(PICO_BOARD pico2_w)
set(PICO_PLATFORM rp2350)

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

set(CPU cortex-m33)
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -mcpu=${CPU} -mthumb")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcpu=${CPU} -mthumb")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -mcpu=${CPU} -mthumb")

project(WP_erase C CXX ASM)

# Pico SDK tooling (picotool) requires .elf extension on executables, and WP_erase.uf2 is made from it.
# The SDK normally sets this via Platform/PICO.cmake, but with our custom toolchain
# (CMAKE_SYSTEM_NAME=Generic) it must be set explicitly, after project() (as WP does).
set(CMAKE_EXECUTABLE_SUFFIX .elf)

pico_sdk_init()

add_executable(WP_erase ${NUKE_SOURCE})
target_link_libraries(WP_erase pico_stdlib hardware_flash)

# It makes no sense to run this program from the flash it is erasing: always build a RAM-only binary.
pico_set_binary_type(WP_erase no_flash)

# Generate WP_erase.uf2 for BOOTSEL drag-and-drop
pico_add_extra_outputs(WP_erase)
