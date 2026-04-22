cmake_minimum_required(VERSION 3.13)

if(NOT DEFINED PICO_SDK_PATH)
    message(FATAL_ERROR "PICO_SDK_PATH is undefined: Can't find PicoSDK")
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

project(WpUsbBoot C CXX ASM)
pico_sdk_init()

add_executable(WpUsbBoot main.c)
target_link_libraries(WpUsbBoot pico_stdlib hardware_gpio hardware_clocks hardware_watchdog hardware_ticks)
pico_set_binary_type(WpUsbBoot no_flash)
