cmake_minimum_required(VERSION 3.13)

if(NOT DEFINED PICO_SDK_PATH)
    message(FATAL_ERROR "PICO_SDK_PATH is undefined: Can't find PicoSDK")
endif()

# --------------------------------------------------------------------------------------------------
# These PICO_* definitions need to occur before including pico_sdk_import.cmake:

# Reuse EP's board definition (has DBG_BSY LED pin definition and XOSC config)
set(PICO_BOARD umod4_EP)

# The SDK does not know about our board, so tell it where to find the board definition:
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../EP/src)

set(PICO_PLATFORM rp2040)

# NO_FLASH build - everything runs from RAM
# This tells the SDK to omit boot2 bootloader and configure for RAM-only execution
set(PICO_DEFAULT_BINARY_TYPE no_flash)

# As per SDK doc, this include needs to happen before project()
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

# Prevent errors that will be generated if CMake tries to test the cross-compiler:
set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")

project(SwdReflash
    DESCRIPTION "RAM-resident LED blink test for SWD loading (Phase 0)"
    HOMEPAGE_URL https://www.island-underground.com/projects/umod4
    LANGUAGES C CXX ASM
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# The toolchain file explained where to find the cross-compiler.
# We need to tell that cross-compiler a bit more about how to compile the code for this project.
set(CPU cortex-m0)

set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -mcpu=${CPU} -mthumb")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcpu=${CPU} -mthumb")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -mcpu=${CPU} -mthumb")

# This is required to avoid a linker error: undefined reference to '__cxa_guard_acquire'
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-threadsafe-statics")

# Disable C++ exception processing
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions")

# We will not be using C++ run-time typing features
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")

# Use -Og for debug builds (easier to step through with debugger)
# For Phase 0, code size is not critical, so optimize for debugging
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Og")
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -Og")

pico_sdk_init()

# Include EP's src directory for hardware.h (DBG_BSY_LSB definition)
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../EP/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_executable(${PROJECT_NAME}
    src/main.c
)

# Minimal compile definitions for RAM-only LED blink
target_compile_definitions(${PROJECT_NAME} PRIVATE
    # Very small heap since we don't use malloc for LED blink
    PICO_HEAP_SIZE=1024

    # Confirm we're in no-flash mode
    PICO_NO_FLASH=1

    # No stdio needed for LED blink
    PICO_STDIO_ENABLE_CRLF_SUPPORT=0
    PICO_STDIO_DEFAULT_CRLF=0

    # No malloc mutex needed (single core, no malloc)
    PICO_USE_MALLOC_MUTEX=0
)

# Disable all stdio outputs (no UART, USB, or RTT needed for LED blink)
pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_enable_stdio_usb(${PROJECT_NAME} 0)
pico_enable_stdio_rtt(${PROJECT_NAME} 0)

# Tell the link against pico_runtime (below) to not pull in things like malloc and printf
# that the SDK might consider as 'basic', but are certainly unecessary in this project.
# See https://github.com/raspberrypi/pico-sdk/issues/2250#issuecomment-2646595259
pico_minimize_runtime(${PROJECT_NAME} ...)

target_link_libraries(${PROJECT_NAME}
    pico_runtime       # Basic runtime support
    pico_platform      # Platform definitions
    hardware_gpio      # GPIO control
    hardware_timer     # For busy_wait_us_32()
)

# Print build size after compilation
add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    VERBATIM
    COMMAND ${CROSSCOMPILE_TOOL_PATH}/arm-none-eabi-size ${CMAKE_BINARY_DIR}/${PROJECT_NAME}
    COMMENT "SwdReflash binary size:"
)

# Create useful output files for debugging
pico_add_dis_output(${PROJECT_NAME})  # Disassembly (.dis)
pico_add_map_output(${PROJECT_NAME})  # Linker map (.map)
pico_add_hex_output(${PROJECT_NAME})  # Intel HEX (.hex)

# NOTE: We do NOT create a UF2 file because this program is loaded via debugger,
# not by drag-and-drop to BOOTSEL drive

# Use custom RAM-only linker script
pico_set_linker_script(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/src/memmap_ram.ld)
