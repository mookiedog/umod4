cmake_minimum_required(VERSION 3.19)  # 3.19 required for string(JSON ...)

include(FetchContent)

if(NOT DEFINED LFS_SRC_DIR)
    message(FATAL_ERROR "LFS_SRC_DIR is undefined: Can't find LittleFS")
endif()

if(NOT DEFINED PICO_SDK_PATH)
    message(FATAL_ERROR "PICO_SDK_PATH is undefined: Can't find PicoSDK")
endif()

# --------------------------------------------------------------------------------------------------
# Define the project

# Get rid of errors generated when CMake tries to test the cross-compiler:
set (CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")

# --------------------------------------------------------------------------------------------------
# These definitions need to occur before including pico_sdk_import.cmake:
set(PICO_BOARD pico2_w)

# PICO_PLATFORM controls the toolchain (ARM vs RISC-V), not the security state.
# Valid values: rp2350, rp2350-arm-s, rp2350-riscv
# The UF2 family ID (rp2350-arm-s vs rp2350-arm-ns) is set separately via picotool.
set(PICO_PLATFORM rp2350)
set(CPU cortex-m33)


# As per SDK doc, this include needs to happen before project()
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(WP
  LANGUAGES C CXX ASM
)

# Pico SDK tooling (picotool) requires .elf extension on executables.
# The SDK normally sets this via Platform/PICO.cmake, but since we use a custom
# toolchain with CMAKE_SYSTEM_NAME=Generic, we must set it explicitly here.
set(CMAKE_EXECUTABLE_SUFFIX .elf)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)

if(PICO_SDK_VERSION_STRING VERSION_LESS "2.0.0")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 2.0.0 (or later) required for pico2_w support. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -mcpu=${CPU} -mthumb")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcpu=${CPU} -mthumb")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -mcpu=${CPU} -mthumb")

set(TOOLS_DIR ${CMAKE_BINARY_DIR}/tools)

# Tell the build system where to find the root of the FreeRTOS-Kernel
set(FREERTOS_KERNEL_PATH ${PICO_SDK_PATH}/FreeRTOS-Kernel)

# Important: Use the RP2040 kernel importer!
include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)

#add_subdirectory(${PICO_SDK_PATH}/lib/lwip)

FetchContent_Declare(tiny-regex-c
    GIT_REPOSITORY https://github.com/kokke/tiny-regex-c
    GIT_TAG        f2632c6d9ed25272987471cdb8b70395c2460bdb   # 2024-Oct-25
)

# We use FetchContent_populate, not _add(), because _populate() does not perform an
# automatic addSubdirectory(). We will add it as an INTERFACE library,
# knowing that the sources will reside in TINY_REGEX_C_SRC_DIR.
message(STATUS "Fetching the 'tiny-regex-c' project")
FetchContent_populate(tiny-regex-c)
set(TINY_REGEX_C_SRC_DIR ${CMAKE_BINARY_DIR}/_deps/tiny-regex-c-src)

FetchContent_Declare(sdio_rp2350
    GIT_REPOSITORY https://github.com/rabbitholecomputing/SDIO_RP2350
    GIT_TAG        1.0.6
)

# We use FetchContent_populate for the SDIO library to add it as an INTERFACE library
message(STATUS "Fetching the 'SDIO_RP2350' project")
FetchContent_Populate(sdio_rp2350)
set(SDIO_RP2350_SRC_DIR ${CMAKE_BINARY_DIR}/_deps/sdio_rp2350-src)

# This is required to avoid a linker error: undefined reference to '__cxa_guard_acquire'
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-threadsafe-statics")

# Disable C++ exception processing
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions")

# We will not be using C++ run-time typing features
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")

# Define PICO_FLASH_SIZE_BYTES for the SDK/Linker scripts.
# It is not a trivial thing to tell the linker how much Flash space is available for a WP image.
# The amount of flash is not driven by the size of the SPI flash like normal, but instead,
# by the size of the flash area as defined by partition_table.json.
# This complicated wad of CMake extracts the true size of partition A from partition_table.json
# so the linker knows the real flash budget.
# Note that ORIGIN stays 0x10000000 because the RP2350 bootrom uses QMI
# ATRANS to remap partition A's physical start to XIP_BASE before launching a WP image.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/partition_table.json" _PTABLE)
string(JSON _N_PARTS LENGTH "${_PTABLE}" "partitions")
math(EXPR _N_PARTS_1 "${_N_PARTS} - 1")
foreach(_I RANGE 0 ${_N_PARTS_1})
    string(JSON _PART_NAME GET "${_PTABLE}" "partitions" ${_I} "name")
    if(_PART_NAME STREQUAL "A")
        string(JSON _PART_SIZE_STR GET "${_PTABLE}" "partitions" ${_I} "size")
        break()
    endif()
endforeach()
if(NOT _PART_SIZE_STR)
    message(FATAL_ERROR "Partition 'A' not found in partition_table.json")
endif()
string(REGEX MATCH "^([0-9]+)([KM]?)$" _ "${_PART_SIZE_STR}")
if(CMAKE_MATCH_2 STREQUAL "K")
    math(EXPR PICO_FLASH_SIZE_BYTES "${CMAKE_MATCH_1} * 1024")
elseif(CMAKE_MATCH_2 STREQUAL "M")
    math(EXPR PICO_FLASH_SIZE_BYTES "${CMAKE_MATCH_1} * 1024 * 1024")
else()
    set(PICO_FLASH_SIZE_BYTES "${CMAKE_MATCH_1}")
endif()
message(STATUS "WP partition A flash budget: ${PICO_FLASH_SIZE_BYTES} bytes (${_PART_SIZE_STR})")

pico_sdk_init()

if(PICO_CYW43_SUPPORTED)
    if(NOT TARGET pico_cyw43_arch)
        message(FATAL_ERROR "Pico W support is not available")
    endif()
endif()

# Server config — optional, non-sensitive.
# WiFi credentials are NOT accepted here — they are entered once via AP mode and
# stored in the flash config partition, which persists across firmware reflashes.
if(DEFINED ENV{UMOD4_SERVER_HOST} AND (NOT UMOD4_SERVER_HOST))
    set(UMOD4_SERVER_HOST $ENV{UMOD4_SERVER_HOST})
    message("Using UMOD4_SERVER_HOST from environment ('${UMOD4_SERVER_HOST}')")
endif()

if(DEFINED ENV{UMOD4_SERVER_PORT})
    set(UMOD4_SERVER_PORT $ENV{UMOD4_SERVER_PORT})
    message("Using UMOD4_SERVER_PORT from environment ('${UMOD4_SERVER_PORT}')")
elseif(NOT UMOD4_SERVER_PORT)
    set(UMOD4_SERVER_PORT "8081")
endif()

# Not happy with this...
if(DEFINED ENV{PICO_EXAMPLES_PATH} AND NOT PICO_EXAMPLES_PATH)
    set(PICO_EXAMPLES_PATH $ENV{PICO_EXAMPLES_PATH})
    message("Using PICO_EXAMPLES_PATH from environment ('${PICO_EXAMPLES_PATH}')")
endif()

#add_subdirectory(wifi)

if(NOT TARGET pico_btstack_base)
    message("Skipping Pico W Bluetooth examples as support is not available")
else()
    #add_subdirectory(bt)
endif()


include_directories(
    ${LIB_DIR}/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}  # For generated files like fsdata.c
    # lwIP includes needed for pico_cyw43_arch_lwip_sys_freertos
    ${PICO_SDK_PATH}/lib/lwip/src/include
    ${PICO_SDK_PATH}/src/rp2_common/pico_lwip/include
    ${PICO_SDK_PATH}/lib/lwip/src/include/lwip/apps
    # FreeRTOS lwIP arch port
    ${PICO_SDK_PATH}/lib/lwip/contrib/ports/freertos/include
)

# Generate fsdata.c from www/ directory for embedded web UI
set(MAKEFSDATA_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/tools/makefsdata")
set(WWW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/www")
set(FSDATA_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/fsdata.c")

# Custom target to generate fsdata.c (included by SDK's fs.c, not compiled directly)
# Upload response files (upload_*.json) are now generated dynamically in fs_custom.c
add_custom_command(
    OUTPUT ${FSDATA_OUTPUT}
    COMMAND perl ${MAKEFSDATA_SCRIPT} ${WWW_DIR}
    COMMAND mv ${WWW_DIR}/fsdata.c ${FSDATA_OUTPUT}.tmp
    COMMAND bash -c "echo '#include \"lwip/apps/fs.h\"' > ${FSDATA_OUTPUT} && echo '#include \"lwip/def.h\"' >> ${FSDATA_OUTPUT} && echo '' >> ${FSDATA_OUTPUT} && echo '#define file_NULL (struct fsdata_file *) NULL' >> ${FSDATA_OUTPUT} && echo '' >> ${FSDATA_OUTPUT} && sed 's/{{NULL,/{{file_NULL,/g' ${FSDATA_OUTPUT}.tmp >> ${FSDATA_OUTPUT}"
    COMMAND rm ${FSDATA_OUTPUT}.tmp
    DEPENDS ${MAKEFSDATA_SCRIPT}
            ${WWW_DIR}/index.html
            ${WWW_DIR}/status.html
            ${WWW_DIR}/eprom_info.html
            ${WWW_DIR}/ecu_live.html
            ${WWW_DIR}/wifi_config.html
            ${WWW_DIR}/sd_info.html
            ${WWW_DIR}/ep_stdio.html
            ${WWW_DIR}/style.css
    WORKING_DIRECTORY ${WWW_DIR}
    COMMENT "Generating fsdata.c from www directory"
    VERBATIM
)

# Ensure fsdata.c is generated before building the executable
add_custom_target(generate_fsdata DEPENDS ${FSDATA_OUTPUT})

# Generate embedded SwdReflash binary for SWD programming
set(SWDREFLASH_ELF "${CMAKE_CURRENT_BINARY_DIR}/../SwdReflash/SwdReflash")
set(SWDREFLASH_BIN "${CMAKE_CURRENT_BINARY_DIR}/swdreflash.bin")
set(SWDREFLASH_C "${CMAKE_CURRENT_BINARY_DIR}/swdreflash_binary.c")
set(SWDREFLASH_H "${CMAKE_CURRENT_BINARY_DIR}/swdreflash_binary.h")
set(BIN2C_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../tools/src/bin2c.py")

# Convert SwdReflash ELF to raw binary, then to C array
# Note: Top-level CMakeLists.txt ensures SwdReflash is built before WP,
#       so the ELF file will exist when this command runs
add_custom_command(
    OUTPUT ${SWDREFLASH_C} ${SWDREFLASH_H}
    COMMAND ${CROSSCOMPILE_TOOL_PATH}/arm-none-eabi-objcopy -O binary ${SWDREFLASH_ELF} ${SWDREFLASH_BIN}
    COMMAND python3 ${BIN2C_SCRIPT} ${SWDREFLASH_BIN} ${CMAKE_CURRENT_BINARY_DIR}/swdreflash_binary swdreflash 0x20000101 0x20042000
    DEPENDS ${BIN2C_SCRIPT} ${SWDREFLASH_ELF}
    COMMENT "Embedding SwdReflash binary for SWD programming"
    VERBATIM
)

add_custom_target(generate_swdreflash_binary DEPENDS ${SWDREFLASH_C} ${SWDREFLASH_H})

add_executable(${PROJECT_NAME}
    src/Clock.cpp
    src/Crc.cpp
    src/FlashEp.cpp
    src/FlashWp.cpp
    src/Gps.cpp
    src/malloc_lock.cpp
    src/lfsMgr.cpp
    src/Logger.cpp
    src/main.cpp
    src/NeoPixelConnect.cpp
    src/Psm.cpp
    src/SdCardSDIO.cpp
    src/Shell.cpp
    src/Swd.cpp
    src/TimeUtils.cpp
    src/Uart.cpp
    src/WiFiManager.cpp
    src/dhcpserver.c
    src/dns_server.c

    # Shared lib
    ${LIB_DIR}/src/log_meta.c

    # MDL (Motorbike Data Link) files
    src/FlashConfig.cpp
    src/NetworkManager.cpp
    src/api_handlers.cpp
    src/fs_custom.cpp
    src/file_io_task.cpp
    src/upload_handler.cpp
    src/ota_flash_task.cpp
    src/flash_safe_helper.cpp
    # Note: fsdata.c is NOT listed here - it's included by SDK's fs.c via HTTPD_FSDATA_FILE
    src/httpd_stubs.cpp
    src/ep_rtt_forwarder.cpp

    # The RAM-resident SwdReflash binary that gets loaded to EP via SWD:
    ${SWDREFLASH_C}
)

# Force a rebuild of main.cpp every time so we can see its build time change
add_custom_target(always_rebuild)
add_dependencies(${PROJECT_NAME} always_rebuild)
set_source_files_properties(src/main.cpp PROPERTIES OBJECT_DEPENDS always_rebuild)

# Ensure generated files are created before compiling
add_dependencies(${PROJECT_NAME} generate_fsdata generate_swdreflash_binary)

# Suppress volatile warnings from FreeRTOS headers in files that include FreeRTOS
set_source_files_properties(
    src/Shell.cpp
    src/api_handlers.cpp
    src/flash_safe_helper.cpp
    src/FlashConfig.cpp
    src/FlashWp.cpp
    src/file_io_task.cpp
    src/fs_custom.cpp
    src/Gps.cpp
    src/lfsMgr.cpp
    src/Logger.cpp
    src/main.cpp
    src/NetworkManager.cpp
    src/ota_flash_task.cpp
    src/SdCardSDIO.cpp
    src/upload_handler.cpp
    src/WiFiManager.cpp
    src/Uart.cpp
    src/ep_rtt_forwarder.cpp

    PROPERTIES COMPILE_FLAGS "-Wno-volatile"
)

# Include the PIO program header files:
# 32-bit UART RX & TX PIO programs (from shared lib/pio/ directory)
pico_generate_pio_header(${PROJECT_NAME}  ${CMAKE_CURRENT_SOURCE_DIR}/../lib/pio/uart_rx32.pio)
pico_generate_pio_header(${PROJECT_NAME}  ${CMAKE_CURRENT_SOURCE_DIR}/../lib/pio/uart_tx32.pio)

# 4-bit SDIO PIO program for RP2350
pico_generate_pio_header(${PROJECT_NAME}  ${SDIO_RP2350_SRC_DIR}/src/sdio_rp2350.pio)

# Neopixel PIO program
pico_generate_pio_header(${PROJECT_NAME}  ${PICO_EXAMPLES_PATH}/pio/ws2812/ws2812.pio)

# SWD PIO engine
pico_generate_pio_header(${PROJECT_NAME}  ${CMAKE_CURRENT_SOURCE_DIR}/src/swd.pio)

# Override the standard Segger definitions for these symbols:
#add_compile_definitions(SEGGER_RTT_PRINTF_BUFFER_SIZE=1024)
# This is the size of the buffer to use for sending data from the device to the host debug terminal app
add_compile_definitions(BUFFER_SIZE_UP=16384)
# We use 4 up-channels: WP_GEN(0), WP_VFY(1), EP_GEN(2), EP_VFY(3).
# Default is 3; must be raised explicitly.
add_compile_definitions(SEGGER_RTT_MAX_NUM_UP_BUFFERS=4)
# This is the size of the buffer that the host debug terminal app can send down as a single cmdline
add_compile_definitions(BUFFER_SIZE_DOWN=1024)

add_compile_definitions(LFS_THREADSAFE=1)
#add_compile_definitions(LFS_TRACE=printf)

# Disable pico_malloc wrapping - we provide our own malloc protection via malloc_lock.cpp
add_compile_definitions(PICO_USE_MALLOC_MUTEX=0)

# Choose where stdio output goes. It should only be RTT for this project.
pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_enable_stdio_usb(${PROJECT_NAME} 0)
pico_enable_stdio_rtt(${PROJECT_NAME} 1)

# The Littlefs project is not designed using CMake.
# Here, we set up littlefs as an INTERFACE library that can be used by either project.
# To use it, add 'littlefs' to the appropriate project's target_link_libraries() list.
add_library(littlefs INTERFACE)
target_sources(littlefs INTERFACE
    "${LFS_SRC_DIR}/lfs.c"
    "${LFS_SRC_DIR}/lfs_util.c"
)
target_include_directories(littlefs INTERFACE "${LFS_SRC_DIR}")

# Pull in the regex library
add_library(tiny_regex_c INTERFACE)
target_sources(tiny_regex_c INTERFACE
    "${TINY_REGEX_C_SRC_DIR}/re.c"
)
target_include_directories(tiny_regex_c INTERFACE "${TINY_REGEX_C_SRC_DIR}")

# Pull in the BSON library (shared with EP)
add_library(bsonlib INTERFACE)
target_sources(bsonlib INTERFACE
    "${LIB_DIR}/src/bsonlib.cpp"
)
target_include_directories(bsonlib INTERFACE "${LIB_DIR}/inc")

# Pull in the murmur3 hash library (shared with EP)
add_library(murmur3 INTERFACE)
target_sources(murmur3 INTERFACE
    "${LIB_DIR}/src/murmur3.cpp"
)
target_include_directories(murmur3 INTERFACE "${LIB_DIR}/inc")

# Pull in the SDIO_RP2350 library (low-level SDIO functions only, not SdFat integration)
# NOTE: sdio_rp2350.cpp is a locally-patched copy in WP/src/ rather than the fetched version.
# The fetched version (in build/_deps/) is still used for headers and the PIO program.
# When updating the upstream library, also update WP/src/sdio_rp2350.cpp.
add_library(sdio_rp2350 INTERFACE)
target_sources(sdio_rp2350 INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/sdio_rp2350.cpp"
)
target_include_directories(sdio_rp2350 INTERFACE
    "${SDIO_RP2350_SRC_DIR}/include"
)

# Possibly temp: allow WP to include 'hardware.h' from the EP directory to access certain shared info
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../EP/src)

# WP needs to know how to talk to the RP2040 SWD reflasher app
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../SwdReflash/src)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    # Enable "Try Before You Buy" (TBYB) for OTA updates
    # PICO_CRT0_IMAGE_TYPE_TBYB=1

    # Optional compile-time default credentials (used only on first boot if flash config is blank).
    # After first boot, config is read from the flash config partition.
    $<$<BOOL:${UMOD4_SERVER_HOST}>:DEFAULT_SERVER_HOST=\"${UMOD4_SERVER_HOST}\">
    DEFAULT_SERVER_PORT=${UMOD4_SERVER_PORT}

    # Point lwIP httpd to our custom-generated fsdata.c in build directory
    HTTPD_FSDATA_FILE=\"${CMAKE_CURRENT_BINARY_DIR}/fsdata.c\"

    # Hook into the SDK panic() function to print heap stats before halting.
    # See my_panic_handler() in main.cpp.
    PICO_PANIC_FUNCTION=my_panic_handler

    # Disable CYW43 driver logging to stdio
    PICO_CYW43_LOGGING_ENABLED=0

    # There are still some messages that get printed from the CYW43 driver,
    # but I have not figured out how to disable them.
    # CYW43_PRINTF() is defined in cyw43_debug.h

    # Note that there can be way more WiFi debug logging from lwIP itself.
    # See: src/lwipopts.h
)

# Print some basic information regarding the build size
add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    VERBATIM
    COMMAND ${CROSSCOMPILE_TOOL_PATH}/arm-none-eabi-size $<TARGET_FILE:${PROJECT_NAME}>
)

# pull in common dependencies
target_link_libraries(${PROJECT_NAME}
    pico_runtime
    pico_stdlib
    pico_multicore
    pico_sync
    pico_bootrom
    pico_flash
    hardware_flash
    hardware_timer
    cmsis_core
    hardware_pio
    hardware_dma
    hardware_uart
    hardware_spi
    FreeRTOS-Kernel
    littlefs
    tiny_regex_c
    bsonlib
    murmur3
    sdio_rp2350
    pico_cyw43_arch_lwip_sys_freertos
    pico_lwip_http
    pico_lwip_mdns
    pico_sha256
)

# Output memory usage stats
target_link_options(${PROJECT_NAME} PRIVATE -Wl,--print-memory-usage)

# This is the partition table our app expects to use
set(PARTITION_TABLE_JSON ${CMAKE_CURRENT_SOURCE_DIR}/partition_table.json)

# Create all output files (.uf2, .bin, .hex, .dis, .map) using SDK's standard method
# This calls picotool_postprocess_binary() which handles:
# - Partition table embedding (from pico_embed_pt_in_binary above)
# - UF2 generation
# - Any signing/hashing if configured
pico_add_extra_outputs(${PROJECT_NAME})

# Generate a standalone partition table UF2 for flashing onto a blank system
# before flashing the OTA UF2 build.
set(PARTITION_TABLE_UF2 ${CMAKE_BINARY_DIR}/partition_table.uf2)
add_custom_command(
    OUTPUT ${PARTITION_TABLE_UF2}
    COMMAND picotool partition create ${PARTITION_TABLE_JSON} ${PARTITION_TABLE_UF2}
    DEPENDS ${PARTITION_TABLE_JSON}
    COMMENT "Generating standalone partition table UF2 from ${PARTITION_TABLE_JSON}"
    VERBATIM
)

# Custom target to build standalone partition table
add_custom_target(partition_table ALL
    DEPENDS ${PARTITION_TABLE_UF2}
    COMMENT "Standalone partition table UF2 ready at ${PARTITION_TABLE_UF2}"
)

# Extract raw binary from partition table UF2 for use by the Partition-Aware debug config.
# OpenOCD flash write_image does not support UF2; it needs a raw .bin + base address.
set(PARTITION_TABLE_BIN ${CMAKE_CURRENT_BINARY_DIR}/partition_table.bin)
add_custom_command(
    OUTPUT ${PARTITION_TABLE_BIN}
    COMMAND python3 -c
        "import struct,sys; d=open(sys.argv[1],'rb').read(); open(sys.argv[2],'wb').write(b''.join(d[i*512+32:i*512+288] for i in range(len(d)//512)))"
        ${PARTITION_TABLE_UF2} ${PARTITION_TABLE_BIN}
    DEPENDS ${PARTITION_TABLE_UF2}
    COMMENT "Extracting partition table binary from UF2"
    VERBATIM
)
add_custom_target(partition_table_bin DEPENDS ${PARTITION_TABLE_BIN})
