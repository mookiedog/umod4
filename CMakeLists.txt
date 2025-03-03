# Hmmm needs to be 3.24 to be able to use DOWNLOAD_EXTRACT_TIMESTAMP
cmake_minimum_required(VERSION 3.22)

# set (CMAKE_VERBOSE_MAKEFILE ON)

include(FetchContent)
include(ExternalProject)

# It is not possible to build umod4 as a single CMake project. The major sticking point
# is that there can only be one instance of an RPi PicoSDK configured for a specific board
# in a CMake namespace. This project needs to define two different board files, one for the EP
# and another for the WP.
# To resolve this, the umod4 project is defined as a CMake "superbuild" where the
# sub-parts that make up the entire project are pulled in via ExternalProject_Add().
# Using ExternalProject_Add() means that each external project gets its own CMake namespace.
# This allows the PicoSDK to get configured twice: once in an EP-specific fashion, and again
# in a WP-specific fashion.

# It is assumed that there is a 'projects' dir somewhere that is used as the root directory
# for everything this project needs. At a minimum, the directory hierarchy must look like this:
#   projects/
#   ├── FreeRTOS-Kernel
#   ├── pico-sdk
#   └── umod4

set(PROJECTS_PATH ${CMAKE_SOURCE_DIR}/..)

# The Pico SDK is too big to download for every project on this machine that might want to use it.
# Instead, it is assumed that a user has downloaded a single, sharable copy to a known location.
# The directory structure is set up to allow for multiple versions of the SDK to exist beside each other.
# All versions will be located at ~/projects/pico-sdk/<version-tag>, like this:
#   projects/
#   └── pico-sdk
#       ├── 1.5.1
#       └── 2.0.0

# Select the SDK version. This variable gets passed into the WP and EP external projects.
# At the moment, both EP and WP use the same SDK version.
# It would be possible for the WP and EP to use different versions of the SDK, should that need arise.
set(PICO_SDK_VERSION 2.0.0)
set(PICO_SDK_PATH ${PROJECTS_PATH}/pico-sdk/${PICO_SDK_VERSION})

# If this path is specified, the SDK init process will make sure that the picotool sources will get downloaded to this path,
# that the picotool gets built, and that the various sub-projects in this superbuild will know where to find the picotool executable.
set(PICOTOOL_FETCH_FROM_GIT_PATH ${PROJECTS_PATH}/picotool)

# Create a Python Virtual Environment (venv) in the build directory so that we can install a few
# required Python packages without using the normal, system-wide python installation mechanisms.
set(VENV_PATH "${CMAKE_CURRENT_BINARY_DIR}/.venv")

if(EXISTS ${VENV_PATH})
    set(Python3_EXECUTABLE "${VENV_PATH}/bin/python3")
else()
    message(STATUS "   ---> Creating a Python virtual environment for this project")
    find_package (Python3 3.10 COMPONENTS Interpreter)
    execute_process (COMMAND "${Python3_EXECUTABLE}" -m venv "${VENV_PATH}")

    # Use an existing python implementation to bootstrap creation the required virtual environment
    ## update the environment with VIRTUAL_ENV variable (mimic the activate script)
    set (ENV{VIRTUAL_ENV} "${VENV_PATH}")
    ## change the context of the search
    set (Python3_FIND_VIRTUALENV FIRST)
    ## unset Python3_EXECUTABLE because it is also an input variable (see documentation, Artifacts Specification section)
    unset (Python3_EXECUTABLE)
    ## Launch a new search
    find_package (Python3 COMPONENTS Interpreter Development)

    # Use the 'pip' from within the new virtual enviroment to install the packages
    message(STATUS "   ---> Installing python lib: pymongo")
    execute_process(COMMAND ${Python3_EXECUTABLE} -m pip install pymongo)

    message(STATUS "   ---> Installing python lib: JSON_minify")
    execute_process(COMMAND ${Python3_EXECUTABLE} -m pip install JSON_minify)

    message(STATUS "   ---> Installing python lib: mmh3")
    execute_process(COMMAND ${Python3_EXECUTABLE} -m pip install mmh3)
endif()

project(umod4)

# Littlefs is a small enough project that we just pull it into our superbuild's build tree
FetchContent_Declare(littlefs
    GIT_REPOSITORY https://github.com/littlefs-project/littlefs
    GIT_TAG v2.9.3
)

# We use FetchContent_populate, not _add(), because _populate() does not perform an
# automatic addSubdirectory(). Sub-projects that need to use littlefs must add
# it as an INTERFACE library, knowing that the sources will reside in LFS_SRC_DIR.
message(STATUS "Fetching the 'littlefs' project")
FetchContent_populate(littlefs)
set(LFS_SRC_DIR ${FETCHCONTENT_BASE_DIR}/littlefs-src)

# Inform the external projects where to find the library files for this project.
# Implicitly, the library source files will be located at ${LIB_DIR}/src, and the
# header files will be located at ${LIB_DIR}/inc
set(LIB_DIR ${CMAKE_SOURCE_DIR}/lib)

# Inform the other external projects where the tools external project will store the tool binaries that it creates:
# The source dir is required for accessing python scripts
set(TOOLS_INSTALL_DIR ${CMAKE_BINARY_DIR}/tools)
set(TOOLS_SOURCE_DIR ${CMAKE_SOURCE_DIR}/tools/src)

# ---------------------------------------------------------------------------------------------
message(STATUS "Adding the 'tools' project")

set(TOOLS_CMAKE_ARG_LIST
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_SOURCE_DIR}/cmake/toolchains/host.cmake
    -DLIB_DIR=${LIB_DIR}
    -DTOOLS_INSTALL_DIR=${TOOLS_INSTALL_DIR}
)

ExternalProject_Add(tools
    PREFIX ${CMAKE_BINARY_DIR}/tools
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/tools
    BINARY_DIR ${CMAKE_BINARY_DIR}/tools
    CMAKE_GENERATOR ${generator}
    CMAKE_ARGS -DCMAKE_BUILD_TYPE:STRING=Debug ${TOOLS_CMAKE_ARG_LIST}
    BUILD_ALWAYS true
    INSTALL_COMMAND ""
)

# ---------------------------------------------------------------------------------------------
message(STATUS "Adding the 'ecu' project")

set(ECU_CMAKE_ARG_LIST
    -DLIB_DIR=${LIB_DIR}
    -DTOOLS_SOURCE_DIR=${TOOLS_SOURCE_DIR}
    -DPython3_EXECUTABLE=${Python3_EXECUTABLE}
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_SOURCE_DIR}/cmake/toolchains/host.cmake
)

ExternalProject_Add(ecu
    PREFIX ${CMAKE_BINARY_DIR}/ecu
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/ecu
    BINARY_DIR ${CMAKE_BINARY_DIR}/ecu
    CMAKE_GENERATOR ${generator}
    CMAKE_ARGS -DCMAKE_BUILD_TYPE:STRING=Debug ${ECU_CMAKE_ARG_LIST}
    BUILD_ALWAYS true
    INSTALL_COMMAND ""
)

# Inform the other external projects where the eprom library files end up:
set(EPROM_LIB_DIR ${CMAKE_BINARY_DIR}/eprom_lib)

# ---------------------------------------------------------------------------------------------
message(STATUS "Adding the 'eprom_lib' project")

set(EPROM_LIB_CMAKE_ARG_LIST
    -DTOOLS_SOURCE_DIR=${TOOLS_SOURCE_DIR}
    -DPython3_EXECUTABLE=${Python3_EXECUTABLE}
)

ExternalProject_Add(eprom_lib

    PREFIX ${CMAKE_BINARY_DIR}/eprom_lib
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/eprom_lib
    BINARY_DIR ${EPROM_LIB_DIR}
    CMAKE_GENERATOR ${generator}
    CMAKE_ARGS -DCMAKE_BUILD_TYPE:STRING=Debug ${EPROM_LIB_CMAKE_ARG_LIST}
    BUILD_ALWAYS true
    INSTALL_COMMAND ""
)

# ---------------------------------------------------------------------------------------------
message(STATUS "Adding the 'EP' project")

set(EP_CMAKE_ARG_LIST
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_SOURCE_DIR}/cmake/toolchains/arm-none-eabi.cmake
    -DPICOTOOL_FETCH_FROM_GIT_PATH=${PICOTOOL_FETCH_FROM_GIT_PATH}
    -DLIB_DIR=${LIB_DIR}
    -DEPROM_LIB_DIR=${EPROM_LIB_DIR}
    -DECU_BINARY_DIR=${CMAKE_BINARY_DIR}/ecu
    -DPICO_SDK_PATH=${PICO_SDK_PATH}
#    -DLFS_SRC_DIR=${LFS_SRC_DIR}
)

ExternalProject_Add(EP
    DEPENDS tools ecu eprom_lib
    PREFIX ${CMAKE_BINARY_DIR}/EP
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/EP
    BINARY_DIR ${CMAKE_BINARY_DIR}/EP
    CMAKE_GENERATOR ${generator}
    CMAKE_ARGS -DCMAKE_BUILD_TYPE:STRING=Debug ${EP_CMAKE_ARG_LIST}
    BUILD_ALWAYS true
    INSTALL_COMMAND ""
)

# ---------------------------------------------------------------------------------------------
message(STATUS "Adding the 'WP' project")

set(WP_CMAKE_ARG_LIST
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_SOURCE_DIR}/cmake/toolchains/arm-none-eabi.cmake
    -DPICOTOOL_FETCH_FROM_GIT_PATH=${PICOTOOL_FETCH_FROM_GIT_PATH}
    -DPROJECTS_PATH=${PROJECTS_PATH}
    -DPICO_SDK_PATH=${PICO_SDK_PATH}
    -DLFS_SRC_DIR=${LFS_SRC_DIR}
)

ExternalProject_Add(WP
    DEPENDS tools EP
    PREFIX ${CMAKE_BINARY_DIR}/WP
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/WP
    BINARY_DIR ${CMAKE_BINARY_DIR}/WP
    CMAKE_GENERATOR ${generator}
    CMAKE_ARGS -DCMAKE_BUILD_TYPE:STRING=Debug ${WP_CMAKE_ARG_LIST}
    BUILD_ALWAYS true
    INSTALL_COMMAND ""
)
