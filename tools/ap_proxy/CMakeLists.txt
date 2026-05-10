cmake_minimum_required(VERSION 3.13)

# Part of the umod4 superbuild — built as an ExternalProject.
# Output: build/ap_proxy/ap_proxy.elf and ap_proxy.uf2
#
# Standalone build also works:
#   cd tools/ap_proxy
#   cmake -GNinja -B build
#   cmake --build build

set(PICO_BOARD pico_w)

if(NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH "$ENV{HOME}/projects/pico-sdk/2.2.0")
endif()

# Locate the ARM toolchain — same base path used by the umod4 superbuild.
if(NOT DEFINED PICO_TOOLCHAIN_PATH)
    file(GLOB _tc_dirs "/opt/arm/arm-none-eabi/*")
    if(_tc_dirs)
        list(SORT _tc_dirs ORDER DESCENDING)
        list(GET _tc_dirs 0 _tc_latest)
        set(PICO_TOOLCHAIN_PATH "${_tc_latest}/bin")
        message(STATUS "Auto-detected ARM toolchain: ${PICO_TOOLCHAIN_PATH}")
    else()
        message(FATAL_ERROR "ARM toolchain not found in /opt/arm/arm-none-eabi/")
    endif()
endif()

# Suppress CMake's compiler validation (cross-compiler always fails try_compile).
set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(ap_proxy C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

add_executable(ap_proxy
    main.c
)

target_include_directories(ap_proxy PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(ap_proxy
    pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background
)

# USB CDC serial — no UART output.
# USB CDC-ACM serial — the Pico W USB cable IS the serial connection.
# VID:PID is read from usb_ids.txt (single source of truth for the project).
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/usb_ids.txt" _usb_ids_lines)
foreach(_line IN LISTS _usb_ids_lines)
    if(_line MATCHES "^AP_PROXY_VID=([0-9a-fA-F]+)")
        set(_ap_proxy_vid "0x${CMAKE_MATCH_1}")
    elseif(_line MATCHES "^AP_PROXY_PID=([0-9a-fA-F]+)")
        set(_ap_proxy_pid "0x${CMAKE_MATCH_1}")
    endif()
endforeach()
message(STATUS "ap_proxy USB VID:PID = ${_ap_proxy_vid}:${_ap_proxy_pid}")

target_compile_definitions(ap_proxy PRIVATE
    PICO_STDIO_USB_CONNECTION_WITHOUT_DTR=1
    USBD_VID=${_ap_proxy_vid}
    USBD_PID=${_ap_proxy_pid}
)

pico_enable_stdio_usb(ap_proxy 1)
pico_enable_stdio_uart(ap_proxy 0)
pico_enable_stdio_rtt(ap_proxy 1)

pico_add_extra_outputs(ap_proxy)
