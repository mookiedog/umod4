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

# This is probably the wrong way to do this...
# For now, compile all the Cortex code with -Os.
# **** WARNING ****:
# For some reason, if you compile with -Og, the linker aligns each generated BSON doc within
# the BSON partition to start on a word boundary. If you compile with -Os, the linker
# does NOT align the docs to start on a word boundary.
if(0)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Os")
  set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -Os")
  set(BSON_PARTITION_IS_PADDED 0)
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Og")
  set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -Og")
  set(BSON_PARTITION_IS_PADDED 1)
endif()

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
  src/bsonlib.cpp
  src/EpromLoader.cpp
  ${DESCRAMBLER_SOURCE}
)


# Get the Git version information into this build
file(READ ${SYSTEM_METADATA_JSON_PATH} RAW_JSON)
string(REPLACE "\"" "\\\"" ESCAPED_JSON "${RAW_JSON}")
set(NEW_C_CONTENT "const char SYSTEM_JSON[] = \"${ESCAPED_JSON}\";\n")

set(META_C_FILE "${CMAKE_CURRENT_BINARY_DIR}/generated_metadata.c")

# Only write if the content is actually different
if(EXISTS "${META_C_FILE}")
    file(READ "${META_C_FILE}" OLD_C_CONTENT)
endif()

if(NOT "${NEW_C_CONTENT}" STREQUAL "${OLD_C_CONTENT}")
    file(WRITE "${META_C_FILE}" "${NEW_C_CONTENT}")
endif()

target_sources(${PROJECT_NAME} PRIVATE ${META_C_FILE})

# Generate the uart PIO header file for use by this project.
# FYI: the header file will be generated into "build/EP/uart_tx32_pio.h".
pico_generate_pio_header(${PROJECT_NAME}  ${CMAKE_CURRENT_SOURCE_DIR}/src/uart_tx32.pio)

target_compile_definitions(${PROJECT_NAME} PRIVATE
  BSON_PARTITION_IS_PADDED=${BSON_PARTITION_IS_PADDED}
)

# Override the standard Segger definitions for these symbols:
#add_compile_definitions(SEGGER_RTT_PRINTF_BUFFER_SIZE=1024)
# This is the size of the buffer to use for sending data from the device to the host debug terminal app
add_compile_definitions(BUFFER_SIZE_UP=2048)
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

# Specify the eprom library BSON objects that should be added to the build
# by adding their contents as the .c source files generated by the eprom library build process:
set(EPROM_SOURCE_LIST
  RP58.c
  RP58USA.c
  549USA.c
  549EuroC.c
  549EuroKatD.c
  8796505.c
  8796529.c
  8796539.c
  8797512.c
  PA59.c
  Edwards.c
)

# Add the path to the EPROM_LIB_DIR to the front of each of the source files in the list
list(TRANSFORM EPROM_SOURCE_LIST PREPEND "${EPROM_LIB_DIR}/")

# Always add the UM4 eprom BSON object from the ECU sub-project:
list(APPEND EPROM_SOURCE_LIST ${ECU_BINARY_DIR}/UM4.c)

# To explore: this next wad might be simpler if we treated the eprom_lib source files as an INTERFACE library.
# In the meantime:
# The EPROM images included in an ecu project build end up in a special linker partition. This partition is
# stored in flash as a wad of BSON documents. At compile time, the EP code know anything about number or kind of
# ECU images that might be present. At run time, the EP is set up to scan the special BSON partition and
# look for EPROM images. This allows a rider to add or delete ECU EPROM images from the flash partition
# without having to rebuild EP firmware. In essence, the BSON flash partition acts like a simple read-only filesystem.
#
# The problem is that the linker will discard any data that is never referenced. So we need to tell the linker
# to not discard this BSON data section even though it may look like it is never referenced at compile time.
# To do that takes a number of steps, described below.
#
# First, the EPROM images are compiled so that they get placed inside a special linker data section called ".bsonStore".
# The linker file will contain a command to 'KEEP' the ".bsonStore" data section.
# This tells the linker to not throw away anything compiled into the build inside that section, even if
# the data is never explicitily referenced.
#
# Defining the library of EPROM images as an 'OBJECT' library means that each image is compiled separately
# to its own .obj file.
# When a target links to an OBJECT library, the build system includes the .obj files in the OBJECT library
# as though they were part of the build, not part of a traditional STATIC library. The crucial difference:
#  - Things inside OBJECT libraries start off 'inside' the build, so the linker honors their 'KEEP' requests.
#  - Things in a STATIC library start off 'outside' the build, and ojects in the STATIC library never get
#    pulled in unless explicitly referenced, regardless if they belong to a section marked with a 'KEEP' request.
#
# At some point in the future, the BSON objects might get moved into a filesystem, or perhaps they might
# get moved out of the EP entirely. In the long run, it might be better to construct the image from the BSON
# components on a phone app or something like that. The constructed image would be stored at a well-known
# address in the flash for the EP to load at boot time. That would be the fasted, most reliable way of storing
# the default image to use at power-up.
add_library(images OBJECT
  ${EPROM_SOURCE_LIST}
)

# Include the murmur3 calculator as an INTERFACE library
add_library(murmur3 INTERFACE)
target_sources(murmur3 INTERFACE
  "${LIB_DIR}/src/murmur3.cpp"
)
target_include_directories(murmur3 INTERFACE "${LIB_DIR}/inc")

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
  images
  #littlefs
  murmur3
  tx_encoder
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

# Use my own linker script.
pico_set_linker_script(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/src/memmap_eprom.ld)
