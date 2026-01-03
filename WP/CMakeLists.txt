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
    set(UMOD4_SERVER_PORT "8888")
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
    # lwIP includes needed for pico_cyw43_arch_lwip_sys_freertos
    ${PICO_SDK_PATH}/lib/lwip/src/include
    ${PICO_SDK_PATH}/src/rp2_common/pico_lwip/include
    ${PICO_SDK_PATH}/lib/lwip/src/include/lwip/apps
    # FreeRTOS lwIP arch port
    ${PICO_SDK_PATH}/lib/lwip/contrib/ports/freertos/include
)

add_executable(${PROJECT_NAME}
    src/Clock.cpp
    src/Crc.cpp
    src/FlashEp.cpp
    src/Gps.cpp
    src/Heap.cpp
    src/Logger.cpp
    src/main.cpp
    src/NeoPixelConnect.cpp
    src/Psm.cpp
    src/SdCard.cpp
    src/SdCardSDIO.cpp
    src/Shell.cpp
    src/Spi.cpp
    src/TimeUtils.cpp
    src/Uart.cpp
    src/WiFiManager.cpp
    src/HttpClient.cpp
    src/LogUploader.cpp
)

# Suppress volatile warnings from FreeRTOS headers in files that include FreeRTOS
set_source_files_properties(
    src/Heap.cpp
    src/Shell.cpp
    src/Gps.cpp
    src/Uart.cpp
    src/Logger.cpp
    src/main.cpp
    src/SdCard.cpp
    src/SdCardSDIO.cpp
    src/WiFiManager.cpp
    src/HttpClient.cpp
    src/LogUploader.cpp
    PROPERTIES COMPILE_FLAGS "-Wno-volatile"
)

# Include the PIO program header files:
pico_generate_pio_header(${PROJECT_NAME}  ${CMAKE_CURRENT_SOURCE_DIR}/src/uart_rx32.pio)
pico_generate_pio_header(${PROJECT_NAME}  ${SDIO_RP2350_SRC_DIR}/src/sdio_rp2350.pio)
pico_generate_pio_header(${PROJECT_NAME}  ${PICO_EXAMPLES_PATH}/pio/ws2812/ws2812.pio)

# Override the standard Segger definitions for these symbols:
#add_compile_definitions(SEGGER_RTT_PRINTF_BUFFER_SIZE=1024)
# This is the size of the buffer to use for sending data from the device to the host debug terminal app
add_compile_definitions(BUFFER_SIZE_UP=16384)
# This is the size of the buffer that the host debug terminal app can send down as a single cmdline
add_compile_definitions(BUFFER_SIZE_DOWN=1024)

add_compile_definitions(LFS_THREADSAFE=1)
#add_compile_definitions(LFS_TRACE=printf)

# We will define new/delete ourselves
add_compile_definitions(PICO_CXX_DISABLE_ALLOCATION_OVERRIDES=1)

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

# Pull in the RP2040 SWD reflash code from its base project as an interface library.
FetchContent_Declare(swd_reflash
    GIT_REPOSITORY https://github.com/brucemack/kc1fsz-tools-cpp
    GIT_TAG main
)

# We use FetchContent_populate, not _add(), because _populate() does not perform an
# automatic addSubdirectory(). We will be adding the project as an
# INTERFACE library.
message(STATUS "Fetching the 'RP2040 SWD_Reflash' project")
FetchContent_populate(swd_reflash)
set(SWD_REFLASH_SRC_DIR ${FETCHCONTENT_BASE_DIR}/swd_reflash-src)

add_library(swd_reflash INTERFACE)
target_sources(swd_reflash INTERFACE
    "${SWD_REFLASH_SRC_DIR}/src/SWDUtils.cpp"
    "${SWD_REFLASH_SRC_DIR}/src/rp2040/SWDDriver.cpp"
)
target_include_directories(swd_reflash INTERFACE
    "${SWD_REFLASH_SRC_DIR}/include"
    "${SWD_REFLASH_SRC_DIR}/include/kc1fsz-tools"
    "${SWD_REFLASH_SRC_DIR}/include/kc1fsz-tools/rp2040"
    )

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
    UMOD4_SERVER_PORT=\"${UMOD4_SERVER_PORT}\"

    # Disable CYW43 driver logging to stdio
    PICO_CYW43_LOGGING_ENABLED=0

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
    FreeRTOS-Kernel-Heap4  # Required for WiFi async context
    littlefs
    swd_reflash
    tiny_regex_c
    sdio_rp2350
    pico_cyw43_arch_lwip_sys_freertos
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
