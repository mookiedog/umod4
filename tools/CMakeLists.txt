cmake_minimum_required(VERSION 3.13)

# This CMakeLists.txt builds the tools required by other parts of this project.
# All tools are compiled to run on the host's OS.
#
# This project is meant to be included as a CMake ExternalProject. The CMakeLists.txt
# that adds this ExternalProject must define the following variables:
#   LIB_DIR (the path to the directory containing the shared source files)
#   TOOLS_INSTALL_DIR (the path to where the tools should be installed after getting built by this file)

if (NOT DEFINED LIB_DIR)
  message(FATAL ": Variable LIB_DIR must be defined!")
endif()

if (NOT DEFINED TOOLS_INSTALL_DIR)
  message(FATAL ": Variable TOOLS_INSTALL_DIR must be defined!")
endif()

project(tools)

# This works because there is only 1 tool that needs to be built
# Get the checksum tool built:
add_executable(checksum
  src/checksum.cpp
)

target_include_directories(checksum PRIVATE ${LIB_DIR}/inc)

add_custom_target(checksum-wrapper
  DEPENDS checksum
)

add_custom_command(
  TARGET checksum POST_BUILD
  VERBATIM
  COMMAND ${CMAKE_COMMAND} -E copy checksum ${TOOLS_INSTALL_DIR}
)
