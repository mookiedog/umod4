cmake_minimum_required(VERSION 3.13)

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

# Based on the board definition, define the appropriate platform and cpu:
if (${PICO_BOARD} STREQUAL "pico_w")
    set(PICO_PLATFORM rp2040)
    set(CPU cortex-m0)
elseif (${PICO_BOARD} STREQUAL "pico2_w")
    set(PICO_PLATFORM rp2350)
    set(CPU cortex-m33)
else()
    message(FATAL_ERROR "Unknown board type!")
endif()

# As per SDK doc, this include needs to happen before project()
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(WP
  LANGUAGES C CXX ASM
)

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

pico_sdk_init()

if(PICO_CYW43_SUPPORTED)
    if(NOT TARGET pico_cyw43_arch)
        message(FATAL_ERROR "Pico W support is not available")
    endif()
endif()

if(DEFINED ENV{WIFI_SSID} AND (NOT WIFI_SSID))
    set(WIFI_SSID $ENV{WIFI_SSID})
    message("Using WIFI_SSID from environment ('${WIFI_SSID}')")
endif()

if(DEFINED ENV{WIFI_PASSWORD} AND (NOT WIFI_PASSWORD))
    set(WIFI_PASSWORD $ENV{WIFI_PASSWORD})
    message("Using WIFI_PASSWORD from environment")
endif()

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

# Validate required WiFi/server configuration
if(NOT WIFI_SSID OR NOT WIFI_PASSWORD)
    message(FATAL_ERROR "WIFI_SSID and WIFI_PASSWORD environment variables must be set for WiFi builds")
endif()

if(NOT UMOD4_SERVER_HOST)
    message(FATAL_ERROR "UMOD4_SERVER_HOST environment variable must be set (e.g., '192.168.1.100' or 'myserver.local')")
endif()

set(WIFI_SSID "${WIFI_SSID}" CACHE INTERNAL "WiFi SSID for ${PROJECT_NAME}")
set(WIFI_PASSWORD "${WIFI_PASSWORD}" CACHE INTERNAL "WiFi password for ${PROJECT_NAME}")
set(UMOD4_SERVER_HOST "${UMOD4_SERVER_HOST}" CACHE INTERNAL "umod4 server hostname/IP for ${PROJECT_NAME}")
set(UMOD4_SERVER_PORT "${UMOD4_SERVER_PORT}" CACHE INTERNAL "umod4 server port for ${PROJECT_NAME}")

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
set(MAKEFSDATA_SCRIPT "${PICO_SDK_PATH}/lib/lwip/src/apps/http/makefsdata/makefsdata")
set(WWW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/www")
set(FSDATA_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/fsdata.c")

# Custom target to generate fsdata.c (included by SDK's fs.c, not compiled directly)
add_custom_command(
    OUTPUT ${FSDATA_OUTPUT}
    COMMAND perl ${MAKEFSDATA_SCRIPT} ${WWW_DIR}
    COMMAND mv ${WWW_DIR}/fsdata.c ${FSDATA_OUTPUT}.tmp
    COMMAND bash -c "echo '#include \"lwip/apps/fs.h\"' > ${FSDATA_OUTPUT} && echo '#include \"lwip/def.h\"' >> ${FSDATA_OUTPUT} && echo '' >> ${FSDATA_OUTPUT} && echo '#define file_NULL (struct fsdata_file *) NULL' >> ${FSDATA_OUTPUT} && echo '' >> ${FSDATA_OUTPUT} && sed 's/{{NULL,/{{file_NULL,/g' ${FSDATA_OUTPUT}.tmp >> ${FSDATA_OUTPUT} && rm ${FSDATA_OUTPUT}.tmp"
    DEPENDS ${WWW_DIR}/index.html
    WORKING_DIRECTORY ${WWW_DIR}
    COMMENT "Generating fsdata.c from www/ directory"
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
    src/Gps.cpp
    src/malloc_lock.cpp
    src/Logger.cpp
    src/main.cpp
    src/NeoPixelConnect.cpp
    src/Psm.cpp
    src/SdCard.cpp
    src/SdCardSDIO.cpp
    src/Shell.cpp
    src/Spi.cpp
    src/SWDLoader.cpp
    src/TimeUtils.cpp
    src/Uart.cpp
    src/WiFiManager.cpp
    # MDL (Motorbike Data Link) files
    src/NetworkManager.cpp
    src/api_handlers.c
    src/fs_custom.c
    src/file_io_task.c
    src/upload_handler.c
    # Note: fsdata.c is NOT listed here - it's included by SDK's fs.c via HTTPD_FSDATA_FILE
    src/httpd_stubs.c
    # Generated SwdReflash binary (RAM-resident EP flasher, loaded to EP via SWD)
    ${SWDREFLASH_C}
)

# Ensure generated files are created before compiling
add_dependencies(${PROJECT_NAME} generate_fsdata generate_swdreflash_binary)

# Suppress volatile warnings from FreeRTOS headers in files that include FreeRTOS
set_source_files_properties(
    src/Shell.cpp
    src/Gps.cpp
    src/Uart.cpp
    src/Logger.cpp
    src/main.cpp
    src/SdCard.cpp
    src/SdCardSDIO.cpp
    src/WiFiManager.cpp
    src/NetworkManager.cpp
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

# Pull in the SDIO_RP2350 library (low-level SDIO functions only, not SdFat integration)
add_library(sdio_rp2350 INTERFACE)
target_sources(sdio_rp2350 INTERFACE
    "${SDIO_RP2350_SRC_DIR}/src/sdio_rp2350.cpp"
)
target_include_directories(sdio_rp2350 INTERFACE
    "${SDIO_RP2350_SRC_DIR}/include"
)

#add_library(swd_load INTERFACE)
#target_sources(swd_load INTERFACE
#  ${CMAKE_CURRENT_SOURCE_DIR}/src/swd/swd_load.cpp)
#target_include_directories(${PROJECT_NAME} PRIVATE src/swd)

# Possibly temp: allow WP to include 'hardware.h' from the EP directory to access certain shared info
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../EP/src)

# This next section defines a number of options as described in Appendix B of the C/C++ Pico SDK manual.
target_compile_definitions(${PROJECT_NAME} PRIVATE
    # Define these next two here where they are easier to find:
    ##PICO_HEAP_SIZE=2048
    #PICO_STACK_SIZE=4096   I'm still not sure the best way to define this. Right now, it's in the memmap_eprom.ld file.

    # WiFi and server configuration (from environment variables)
    WIFI_SSID=\"${WIFI_SSID}\"
    WIFI_PASSWORD=\"${WIFI_PASSWORD}\"
    UMOD4_SERVER_HOST=\"${UMOD4_SERVER_HOST}\"
    UMOD4_SERVER_PORT=${UMOD4_SERVER_PORT}

    # Point lwIP httpd to our custom-generated fsdata.c in build directory
    HTTPD_FSDATA_FILE=\"${CMAKE_CURRENT_BINARY_DIR}/fsdata.c\"

    # Disable CYW43 driver logging to stdio
    PICO_CYW43_LOGGING_ENABLED=0

    # There are still some messags that get printed from the CYW43 driver,
    # but I have not figured out how to disable them.
    # CYW43_PRINTF() is defined in cyw43_debug.h

    # Note that there can be way more WiFi debug logging from lwIP itself.
    # See: src/lwipopts.h
)

# Print some basic information regarding the build size
add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    VERBATIM
    COMMAND ${CROSSCOMPILE_TOOL_PATH}/arm-none-eabi-size ${CMAKE_BINARY_DIR}/${PROJECT_NAME}
)

# pull in common dependencies
target_link_libraries(${PROJECT_NAME}
    pico_runtime
    pico_stdlib
    pico_multicore
    pico_sync
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
    sdio_rp2350
    pico_cyw43_arch_lwip_sys_freertos
    pico_lwip_http
    pico_lwip_mdns
    pico_sha256  # Hardware-accelerated SHA-256
    #swd_load
)

# Create some useful output files
pico_add_dis_output(${PROJECT_NAME})
pico_add_map_output(${PROJECT_NAME})
pico_add_hex_output(${PROJECT_NAME})

# This is annoying, but I can't figure out how to get picotool to do the right thing otherwise:
add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    VERBATIM
    COMMAND picotool uf2 convert --quiet ${CMAKE_BINARY_DIR}/${PROJECT_NAME} -t elf ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.uf2 --family ${PICO_PLATFORM}
)
