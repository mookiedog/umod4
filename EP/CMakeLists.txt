cmake_minimum_required(VERSION 3.13)

if(NOT DEFINED PICO_SDK_PATH)
  message(FATAL_ERROR "PICO_SDK_PATH is undefined: Can't find PicoSDK")
endif()

# --------------------------------------------------------------------------------------------------
# These PICO_* definitions need to occur before including pico_sdk_import.cmake:

# Define the board file that the SDK should use for this build:
set(PICO_BOARD umod4_EP)

# The SDK does not know about our board, so tell it where to find the board definition:
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(PICO_PLATFORM rp2040)

# As per SDK doc, this include needs to happen before project()
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

# Prevent errors that will be generated if CMake tries to test the cross-compiler:
set (CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")

project(EP
  DESCRIPTION "EP: A 27C256 EPROM/RAM Emulator with Datalogging"
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

# For now, compile all the Cortex code with -Os.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Os")
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -Os")

pico_sdk_init()

# If an unprotect project containing descrambler source exists at the same level
# as the top-level directory of this whole project then add its decoder routine to our build.
set(DESCRAMBLE_SRC_DIR ${CMAKE_SOURCE_DIR}/../../Unprotect/src)
if(EXISTS ${DESCRAMBLE_SRC_DIR}/descramble.cpp)
  message(STATUS "Found a descrambler!")
  set(DESCRAMBLER_SOURCE ${DESCRAMBLE_SRC_DIR}/descramble.cpp)
  add_compile_definitions(HAS_DESCRAMBLER=1)
else()
  message(STATUS "No descrambler option!")
endif()

include_directories(
  ${LIB_DIR}/inc
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_executable(${PROJECT_NAME}
  src/main.cpp
  src/epromEmulator.S
  src/EpromLoader.cpp
  src/busFailsafeSetup.cpp
  ${DESCRAMBLER_SOURCE}
)


# Generate the uart PIO header file for use by this project.
# FYI: the header file will be generated into "build/EP/uart_tx32_pio.h".
pico_generate_pio_header(${PROJECT_NAME}  ${CMAKE_CURRENT_SOURCE_DIR}/src/uart_tx32.pio)

# Generate the bus failsafe PIO header file.
# FYI: the header file will be generated into "build/EP/busFailsafe_pio.h".
pico_generate_pio_header(${PROJECT_NAME}  ${CMAKE_CURRENT_SOURCE_DIR}/src/busFailsafe.pio)

# Override the standard Segger definitions for these symbols.
# The "up" buffer is EP->host printf data. We make it huge as a means of logging boot messages
# that can be retrieved by attaching a debugger *after* the system has booted.
add_compile_definitions(BUFFER_SIZE_UP=32768)
# This is the size of the buffer that the host debug terminal app can send down as a single cmdline
add_compile_definitions(BUFFER_SIZE_DOWN=1024)

# Choose where stdio output goes. It should only be RTT for this project.
pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_enable_stdio_usb(${PROJECT_NAME} 0)
pico_enable_stdio_rtt(${PROJECT_NAME} 1)


if(DEFINED DESCRAMBLER_SOURCE)
  message(STATUS "Adding descramble source dir to include path")
  # If we have access to a descrambler source tree, make sure to add the path to its header file
  target_include_directories(${PROJECT_NAME} PRIVATE
    ${DESCRAMBLE_SRC_DIR}
  )
endif()

# Watch the bin_files directory so that adding/removing .bin files triggers a CMake
# reconfigure automatically. No clean rebuild needed when a user adds a new .bin file.
set_property(DIRECTORY APPEND PROPERTY
    CMAKE_CONFIGURE_DEPENDS "${EPROM_LIB_SRC_DIR}/bin_files")

# Discover which .bin files currently exist (for build dependency tracking)
file(GLOB EXISTING_BIN_FILES "${EPROM_LIB_SRC_DIR}/bin_files/*.bin")

# Include the murmur3 calculator as an INTERFACE library
add_library(murmur3 INTERFACE)
target_sources(murmur3 INTERFACE
  "${LIB_DIR}/src/murmur3.cpp"
)
target_include_directories(murmur3 INTERFACE "${LIB_DIR}/inc")

# Include the BSON library as an INTERFACE library
add_library(bsonlib INTERFACE)
target_sources(bsonlib INTERFACE
  "${LIB_DIR}/src/bsonlib.cpp"
)
target_include_directories(bsonlib INTERFACE "${LIB_DIR}/inc")

# Include the EP to WP transmission encoder array as an interface library.
add_library(tx_encoder INTERFACE)
target_sources(tx_encoder INTERFACE
  ${CMAKE_CURRENT_BINARY_DIR}/tx_encoder.c
)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/tx_encoder.c
  VERBATIM
  COMMAND ${Python3_EXECUTABLE} ${TOOLS_SOURCE_DIR}/generate_encoder.py -i ${PYTHON_SITEPACKAGES}/Logsyms/Logsyms.py -o ${CMAKE_CURRENT_BINARY_DIR}/tx_encoder.c
  DEPENDS
    ${PYTHON_SITEPACKAGES}/Logsyms/Logsyms.py
    ${TOOLS_SOURCE_DIR}/generate_encoder.py
  COMMENT "Generating ${CMAKE_CURRENT_BINARY_DIR}/tx_encoder.c from Logsyms.py"
)

# -------------------------------------------------------------------------------------
# Image Store Construction
#
# Note: THIS IS FOR TESTING ONLY!
# Under normal circumstances, the EP executable should NOT contain any data that ends
# up in the image store or the image selector, otherwise it will overwrite
# any existing configuration and image data that a rider might have uploaded.
#
# build-image-store.py reads image_store_config.json and produces:
#   image_store/<name>.slot     — one 64K slot file per image (32K BSON header + 32K image)
#   image_store/slot0.slot      — slot 0: selector BSON padded to 64K
#   image_store/image_store.bin — compact binary: slot 0 + all image slots in order
#   image_store_data.c          — C source placing image_store.bin in .image_store section
#
# image_store_data.c is compiled into an OBJECT library and linked into EP.elf.
# The .image_store linker section maps to 0x10200000, so the debugger programs
# the image store to flash automatically when it loads EP.elf.
#
# Set to ON to build the image store and link it into EP.elf.
# When ON, the debugger programs 0x10200000 automatically as part of flashing EP.
# When OFF, 0x10200000 is untouched — EP inherits whatever was there previously,
# or boots into limp mode on a fresh device.
#
# Inputs to this process:
#  - image_store_config.json:
#      Defines what images go in the image store and what the image_selector should look like
#  - EPROM images:
#      Requires .bin files in eprom_lib/bin_files/ for any stock EPROMs listed in
#      image_store_config.json.
#      UM4.bin is always available (built from the ECU assembly).
#
# To force limp mode while debugging (e.g. to test the empty-store path),
# use the VS Code task (ctrl-shft-B) "EP: Erase Image Store Selector" to erase slot 0.
set(IMAGE_STORE_ENABLED OFF)

if(IMAGE_STORE_ENABLED)
  set(IMAGE_STORE_CONFIG_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/image_store_config.json)
  set(IMAGE_STORE_CONFIG ${CMAKE_CURRENT_BINARY_DIR}/image_store_config.json)
  set(IMAGE_STORE_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/image_store)
  set(IMAGE_STORE_BIN ${IMAGE_STORE_OUTPUT_DIR}/image_store.bin)
  set(IMAGE_STORE_DATA_C ${CMAKE_CURRENT_BINARY_DIR}/image_store_data.c)

  add_custom_command(
    OUTPUT ${IMAGE_STORE_CONFIG}
    VERBATIM
    COMMAND bash -c "sed 's|//.*||$<SEMICOLON> s|#.*||' ${IMAGE_STORE_CONFIG_SRC} | jq . > ${IMAGE_STORE_CONFIG}"
    DEPENDS ${IMAGE_STORE_CONFIG_SRC}
    COMMENT "Validating \"${IMAGE_STORE_CONFIG_SRC}\""
  )

  add_custom_command(
    OUTPUT ${IMAGE_STORE_BIN} ${IMAGE_STORE_DATA_C}
    DEPENDS
      ${TOOLS_SOURCE_DIR}/build-image-store.py
      ${IMAGE_STORE_CONFIG}
      ${EXISTING_BIN_FILES}
      ${ECU_BINARY_DIR}/UM4.bin
    VERBATIM
    COMMAND ${Python3_EXECUTABLE} ${TOOLS_SOURCE_DIR}/build-image-store.py
      -c ${IMAGE_STORE_CONFIG}
      -o ${IMAGE_STORE_OUTPUT_DIR}
      --search-path ${EPROM_LIB_SRC_DIR}/bin_files
      --search-path ${ECU_BINARY_DIR}
      --output-c ${IMAGE_STORE_DATA_C}
      -v
    COMMENT "Building image store artifacts from ${IMAGE_STORE_CONFIG}"
  )

  # Compile image_store_data.c into an OBJECT library.
  # The data lives in the .image_store linker section (VMA/LMA = 0x10200000).
  # Linking it into EP.elf causes the debugger to program the image store
  # to flash automatically alongside the firmware.
  add_library(image_store_data OBJECT ${IMAGE_STORE_DATA_C})
  target_link_libraries(${PROJECT_NAME} image_store_data)

  add_custom_target(image_store ALL DEPENDS ${IMAGE_STORE_BIN} ${IMAGE_STORE_DATA_C})
endif()

# -------------------------------------------------------------------------------------
# Limp Mode EPROM Image Construction
#
# The limp mode image is a 32KB EPROM image built into the EP firmware as a C array.
# It is loaded unconditionally when all normal image selection methods fail (empty
# image store, corrupted selector, all hash checks failed, etc.).
# Being compiled into firmware makes it immune to image store corruption.
#
# All .bin files must be exactly 32768 bytes.
# Output: limp_mode_image.h (C array via xxd -i)
#
# Configuration (mutually exclusive, override with cmake -D):
#   LIMP_MODE_COMPLETE_BIN=<name>.bin  - A complete 32KB image from eprom_lib/bin_files/
#   LIMP_MODE_MAPBLOB=<name>.bin       - Combine maps (0x0000-0x1BFF) from <name>.bin
#                                        with code (0x1C00-0x7FFF) from UM4.bin;
#                                        if the file is not found, falls back to UM4.bin
#   (default)                          - Use UM4.bin from the ECU build as a complete image
set(LIMP_MODE_MAPBLOB "RP58.bin")

set(LIMP_MODE_BIN_DIR "${EPROM_LIB_SRC_DIR}/bin_files")
set(LIMP_MODE_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/limp_mode.bin")
set(LIMP_MODE_HEADER "${CMAKE_CURRENT_BINARY_DIR}/limp_mode_image.h")

if(DEFINED LIMP_MODE_COMPLETE_BIN AND LIMP_MODE_MAPBLOB)
    message(FATAL_ERROR "LIMP_MODE_COMPLETE_BIN and LIMP_MODE_MAPBLOB are mutually exclusive")
endif()

if(DEFINED LIMP_MODE_COMPLETE_BIN)
    # Use a complete 32KB image as-is
    set(LIMP_MODE_SRC "${LIMP_MODE_BIN_DIR}/${LIMP_MODE_COMPLETE_BIN}")
    if(NOT EXISTS "${LIMP_MODE_SRC}")
        message(FATAL_ERROR "LIMP_MODE_COMPLETE_BIN: ${LIMP_MODE_SRC} does not exist")
    endif()
    add_custom_command(
        OUTPUT ${LIMP_MODE_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E copy "${LIMP_MODE_SRC}" "${LIMP_MODE_OUTPUT}"
        DEPENDS "${LIMP_MODE_SRC}"
        COMMENT "Limp mode image: using complete image ${LIMP_MODE_COMPLETE_BIN}"
        VERBATIM
    )

elseif(LIMP_MODE_MAPBLOB)
    # Combine maps (0x0000-0x1BFF) from the specified bin with code (0x1C00-0x7FFF) from UM4.bin.
    # If the mapblob .bin is not present, fall back silently to UM4.bin as a complete image.
    set(LIMP_MODE_MAPBLOB_SRC "${LIMP_MODE_BIN_DIR}/${LIMP_MODE_MAPBLOB}")
    set(LIMP_MODE_CODE_SRC "${ECU_BINARY_DIR}/UM4.bin")
    if(EXISTS "${LIMP_MODE_MAPBLOB_SRC}")
        add_custom_command(
            OUTPUT ${LIMP_MODE_OUTPUT}
            COMMAND bash -c "head -c 7168 '${LIMP_MODE_MAPBLOB_SRC}' > '${LIMP_MODE_OUTPUT}' && tail -c +7169 '${LIMP_MODE_CODE_SRC}' >> '${LIMP_MODE_OUTPUT}'"
            DEPENDS "${LIMP_MODE_MAPBLOB_SRC}" "${LIMP_MODE_CODE_SRC}"
            COMMENT "Limp mode image: maps from ${LIMP_MODE_MAPBLOB}, code from UM4.bin"
            VERBATIM
        )
    else()
        message(STATUS "Limp mode: ${LIMP_MODE_MAPBLOB} not found in eprom_lib/bin_files/, falling back to UM4.bin")
        add_custom_command(
            OUTPUT ${LIMP_MODE_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E copy "${LIMP_MODE_CODE_SRC}" "${LIMP_MODE_OUTPUT}"
            DEPENDS "${LIMP_MODE_CODE_SRC}"
            COMMENT "Limp mode image: using UM4.bin (${LIMP_MODE_MAPBLOB} not found)"
            VERBATIM
        )
    endif()

else()
    # Use UM4.bin as a complete image
    set(LIMP_MODE_CODE_SRC "${ECU_BINARY_DIR}/UM4.bin")
    add_custom_command(
        OUTPUT ${LIMP_MODE_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E copy "${LIMP_MODE_CODE_SRC}" "${LIMP_MODE_OUTPUT}"
        DEPENDS "${LIMP_MODE_CODE_SRC}"
        COMMENT "Limp mode image: using UM4.bin"
        VERBATIM
    )
endif()

# Generate C header from the limp mode binary via xxd -i.
# sed makes the array 'const unsigned' (read-only, lives in flash) and renames it to limp_mode_image.
add_custom_command(
    OUTPUT ${LIMP_MODE_HEADER}
    COMMAND bash -c "xxd -i limp_mode.bin | sed 's/unsigned char limp_mode_bin/const unsigned char limp_mode_image/' > limp_mode_image.h"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${LIMP_MODE_OUTPUT}
    COMMENT "Generating limp_mode_image.h"
    VERBATIM
)

add_custom_target(limp_mode_image ALL DEPENDS ${LIMP_MODE_HEADER})
add_dependencies(${PROJECT_NAME} limp_mode_image)

# **********************************************************************************************************************************************
# This next section defines a number of options as described in Appendix B of the C/C++ Pico SDK manual.
target_compile_definitions(${PROJECT_NAME} PRIVATE
  # Define these next two here where they are easier to find:
  PICO_HEAP_SIZE=2048
  #PICO_STACK_SIZE=4096   I'm still not sure the best way to define this. Right now, it's in the memmap_eprom.ld file.

  # This project does not need a malloc mutex because core1 never does any malloc calls
  PICO_USE_MALLOC_MUTEX=0
)

# After the build completes, print some basic information regarding the build size
add_custom_command(
  TARGET ${PROJECT_NAME} POST_BUILD
  VERBATIM
  COMMAND ${CROSSCOMPILE_TOOL_PATH}/arm-none-eabi-size ${CMAKE_BINARY_DIR}/${PROJECT_NAME}
)

target_link_libraries(${PROJECT_NAME}
  pico_runtime
  pico_stdlib
  pico_multicore
  hardware_pio
  hardware_flash
  hardware_timer
  cmsis_core
  murmur3
  bsonlib
  tx_encoder
  )

# Output memory usage stats
target_link_options(${PROJECT_NAME} PRIVATE -Wl,--print-memory-usage)

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

# Use my own linker script.
pico_set_linker_script(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/src/memmap_eprom.ld)
