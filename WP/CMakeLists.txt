cmake_minimum_required(VERSION 3.13)

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
# These PICO_* definitions need to occur before including pico_sdk_import.cmake:

# Define the board file that the SDK should use for this build:
set(PICO_BOARD pico_w)

set(PICO_PLATFORM rp2040)

# As per SDK doc, this include needs to happen before project()
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(WP
  LANGUAGES C CXX ASM
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if(PICO_SDK_VERSION_STRING VERSION_LESS "1.5.1")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.5.1 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

set(CPU cortex-m0)

set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -mcpu=${CPU} -mthumb")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcpu=${CPU} -mthumb")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -mcpu=${CPU} -mthumb")

set(TOOLS_DIR ${CMAKE_BINARY_DIR}/tools)

# ToDo: document this in the build.md
# There is an inherent assumption in this project that the FreeRTOS repository has
# been cloned into the top-level 'pico' directory using something like this:
#   cd to the directory that contains the sdk directory
#   git clone -b smp https://github.com/FreeRTOS/FreeRTOS-Kernel --recurse-submodules
# Selecting the 'smp' branch is important because it contains the RP2040 SMP functionality!

# Tell the build system where to find the root of the FreeRTOS-Kernel
set(FREERTOS_KERNEL_PATH ${PROJECTS_PATH}/FreeRTOS-Kernel)

# Important: Use the RP2040 kernel importer!
include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)

#add_subdirectory(${PICO_SDK_PATH}/lib/lwip)


# This is required to avoid a linker error: undefined reference to '__cxa_guard_acquire'
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-threadsafe-statics")

# Disable C++ exception processing
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions")

# Turn off RTTI since we have our own printf mechanisms
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")

pico_sdk_init()

if(PICO_CYW43_SUPPORTED) # set by PICO_BOARD=pico_w
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

set(WIFI_SSID "${WIFI_SSID}" CACHE INTERNAL "WiFi SSID for ${PROJECT_NAME}")
set(WIFI_PASSWORD "${WIFI_PASSWORD}" CACHE INTERNAL "WiFi password for ${PROJECT_NAME}")

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
)

add_executable(${PROJECT_NAME}
  src/Clock.cpp
  src/Crc.cpp
  src/Gps.cpp
  src/main.cpp
  src/NeoPixelConnect.cpp
  src/Psm.cpp
  src/SdCard.cpp
  src/Spi.cpp
  src/TimeUtils.cpp
  src/Uart.cpp
  src/ws2812.pio

)

# The Littlefs project is not designed using CMake.
# Here, we set up littlefs as an INTERFACE library that can be used by either project.
# To use it, add 'littlefs' to the appropriate project's target_link_libraries() list.
add_library(littlefs INTERFACE)
target_sources(littlefs INTERFACE
  "${LFS_SRC_DIR}/lfs.c"
  "${LFS_SRC_DIR}/lfs_util.c"
)
target_include_directories(littlefs INTERFACE "${LFS_SRC_DIR}")


# This next section defines a number of options as described in Appendix B of the C/C++ Pico SDK manual.
target_compile_definitions(${PROJECT_NAME} PRIVATE
  # Define these next two here where they are easier to find:
  ##PICO_HEAP_SIZE=2048
  #PICO_STACK_SIZE=4096   I'm still not sure the best way to define this. Right now, it's in the memmap_eprom.ld file.

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
  hardware_flash
  hardware_timer
  cmsis_core
  hardware_pio
  hardware_dma
  hardware_uart
  hardware_spi
  FreeRTOS-Kernel
  FreeRTOS-Kernel-Heap1
  littlefs

  #pico_cyw43_arch_lwip_sys_freertos
)

# Create some useful output files
pico_add_dis_output(${PROJECT_NAME})
pico_add_map_output(${PROJECT_NAME})
pico_add_hex_output(${PROJECT_NAME})
