# Hmmm needs to be 3.24 to be able to use DOWNLOAD_EXTRACT_TIMESTAMP
cmake_minimum_required(VERSION 3.22)

# Force Ninja generator for all builds (superbuild and external projects)
set(CMAKE_GENERATOR "Ninja" CACHE INTERNAL "Generator" FORCE)

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
#   ├── pico-examples
#   ├── pico-sdk
#   └── umod4

get_filename_component(PROJECTS_PATH "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
set(PICO_EXAMPLES_PATH ${PROJECTS_PATH}/pico-examples)

# The Pico SDK is too big to download for every project on this machine that might want to use it.
# Instead, it is assumed that a user has downloaded a single, sharable copy to a known location.
# The directory structure is set up to allow for multiple versions of the SDK to exist beside each other.
# All versions will be located at ~/projects/pico-sdk/<version-tag>, like this:
#   projects/
#   └── pico-sdk
#       ├── 1.5.1
#       ├── 2.0.0
#       ├── 2.1.1
#       └── <future version>


# Select the SDK version. This variable gets passed into the WP and EP external projects.
# *** WARNING: changing the SDK version requires deleting the build directory!!
set(PICO_SDK_VERSION 2.3.0)
set(PICO_SDK_PATH ${PROJECTS_PATH}/pico-sdk/${PICO_SDK_VERSION})

# FreeRTOS-Kernel lives as its own sibling checkout, independent of the pico-sdk version --
# it is not part of any official pico-sdk release, and pinning it inside a versioned SDK
# directory means it has to be re-vendored by hand every time PICO_SDK_VERSION changes.
# See BUILDING.md for how this checkout was set up and which commit it is pinned to.
set(FREERTOS_KERNEL_PATH ${PROJECTS_PATH}/FreeRTOS-Kernel)

# .vscode/launch.json needs to know PICO_SDK_PATH too (for its svdFile entries), but
# launch.json is static JSON with no way to ask CMake for the value directly. Rather than
# relying on an environment variable (which goes stale the moment PICO_SDK_VERSION above
# changes, since nothing forces a shell/WSL restart to pick up a fresh export), maintain a
# fixed-name symlink that always points at the current version. launch.json then references
# the stable path build/pico-sdk-current and never needs editing when the version changes.
set(PICO_SDK_SYMLINK ${CMAKE_BINARY_DIR}/pico-sdk-current)
file(REMOVE ${PICO_SDK_SYMLINK})
file(CREATE_LINK ${PICO_SDK_PATH} ${PICO_SDK_SYMLINK} SYMBOLIC)

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
    ## Launch a new search which will find it in the venv
    find_package (Python3 COMPONENTS Interpreter Development)
endif()

# Get the Python version from the virtual environment.
# We will pass this down to the external projects that need it.
execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')"
    OUTPUT_VARIABLE PYTHON_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE PYTHON_VERSION_RESULT
)

if(NOT PYTHON_VERSION_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to determine Python version from virtual environment")
endif()

message(STATUS "Detected Python version: ${PYTHON_VERSION}")

# At this point: python3 is found, a venv exists, and a specific version has been identified.
# Bundle those symbols to pass to all external_projects
set(PYTHON_DEFS
    -DPython3_EXECUTABLE=${Python3_EXECUTABLE}
    -DVENV_PATH=${VENV_PATH}
    -DPYTHON_VERSION=${PYTHON_VERSION}
    -DPYTHON_SITEPACKAGES=${VENV_PATH}/lib/python${PYTHON_VERSION}/site-packages
)

# Use the 'pip' from within the new virtual enviroment to install the packages
message(STATUS "   ---> Installing python lib: pymongo")
execute_process(COMMAND ${Python3_EXECUTABLE} -m pip install pymongo)

message(STATUS "   ---> Installing python lib: JSON_minify")
execute_process(COMMAND ${Python3_EXECUTABLE} -m pip install JSON_minify)

message(STATUS "   ---> Installing python lib: mmh3")
execute_process(COMMAND ${Python3_EXECUTABLE} -m pip install mmh3)

message(STATUS "   ---> Installing python lib: numpy")
execute_process(COMMAND ${Python3_EXECUTABLE} -m pip install numpy)

message(STATUS "   ---> Installing python lib: h5py")
execute_process(COMMAND ${Python3_EXECUTABLE} -m pip install h5py)

message(STATUS "   ---> Installing python lib: pyside6-essentials")
execute_process(COMMAND ${Python3_EXECUTABLE} -m pip install pyside6-essentials)

message(STATUS "   ---> Installing python lib: pyqtgraph")
execute_process(COMMAND ${Python3_EXECUTABLE} -m pip install pyqtgraph)

message(STATUS "   ---> Installing python lib: pandas")
execute_process(COMMAND ${Python3_EXECUTABLE} -m pip install pandas)

message(STATUS "   ---> Installing python lib: flask")
execute_process(COMMAND ${Python3_EXECUTABLE} -m pip install flask)

message(STATUS "   ---> Installing python lib: flask-cors")
execute_process(COMMAND ${Python3_EXECUTABLE} -m pip install flask-cors)

message(STATUS "   ---> Installing python lib: sqlalchemy")
execute_process(COMMAND ${Python3_EXECUTABLE} -m pip install sqlalchemy)

message(STATUS "   ---> Installing python lib: requests")
execute_process(COMMAND ${Python3_EXECUTABLE} -m pip install requests)

message(STATUS "   ---> Installing python lib: PyYAML")
execute_process(COMMAND ${Python3_EXECUTABLE} -m pip install PyYAML)

message(STATUS "   ---> Installing python lib: nuitka")
execute_process(COMMAND ${Python3_EXECUTABLE} -m pip install nuitka)

message(STATUS "   ---> Installing python lib: pyserial")
execute_process(COMMAND ${Python3_EXECUTABLE} -m pip install pyserial)

project(umod4)

# Set generator for external projects
set(generator "Ninja")

# Littlefs is a small enough project that we just pull it into our superbuild's build tree
FetchContent_Declare(littlefs
    GIT_REPOSITORY https://github.com/littlefs-project/littlefs
    GIT_TAG v2.11.2
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
set(TOOLS_INSTALL_DIR $ENV{HOME}/.local/bin)
set(TOOLS_SOURCE_DIR ${CMAKE_SOURCE_DIR}/tools/src)

# ---------------------------------------------------------------------------------------------
message(STATUS "Adding the 'picotool' project")

ExternalProject_Add(picotool
    PREFIX ${CMAKE_BINARY_DIR}/picotool
    GIT_REPOSITORY https://github.com/raspberrypi/picotool.git
    GIT_TAG ${PICO_SDK_VERSION}
    GIT_SHALLOW TRUE
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=$ENV{HOME}/.local
        -DPICO_SDK_PATH=${PICO_SDK_PATH}
    INSTALL_COMMAND
        cmake --install .
        COMMAND ${CMAKE_COMMAND} -E make_directory $ENV{HOME}/.local/share/picotool
        COMMAND ${CMAKE_COMMAND} -E copy
            <SOURCE_DIR>/udev/60-picotool.rules
            $ENV{HOME}/.local/share/picotool/60-picotool.rules
        COMMAND ${CMAKE_COMMAND}
            -DRULES_SRC=$ENV{HOME}/.local/share/picotool/60-picotool.rules
            -P ${CMAKE_SOURCE_DIR}/cmake/check_udev_rules.cmake
)

# ---------------------------------------------------------------------------------------------
message(STATUS "Adding the 'tools' project")

set(TOOLS_CMAKE_ARG_LIST
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_SOURCE_DIR}/cmake/toolchains/host.cmake
    -DLIB_DIR=${LIB_DIR}
    -DTOOLS_INSTALL_DIR=${TOOLS_INSTALL_DIR}
    ${PYTHON_DEFS}
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
# Generate build metadata JSON for injection into ECU firmware
# Each of those subsystems will make sure to log their version info so that a log
# will always be able to identify what software versions were used to create the log.

# 1. Get the current Git Identity
execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_ID
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# 2. Get the Timestamp
string(TIMESTAMP BUILD_TIME "%Y-%m-%d %H:%M:%S")
set(NEW_META "{\"GH\":\"${GIT_ID}\",\"BT\":\"${BUILD_TIME}\"}")

# 3. SMART WRITE: Only update the file if the HASH changed
set(META_FILE "${CMAKE_BINARY_DIR}/system_metadata.json")
if(EXISTS "${META_FILE}")
    file(READ "${META_FILE}" OLD_META)
    # Extract only the Hash part for comparison
    string(REGEX MATCH "\"GH\":\"[^\"]*\"" OLD_HASH "${OLD_META}")
    string(REGEX MATCH "\"GH\":\"[^\"]*\"" NEW_HASH "${NEW_META}")
else()
    set(OLD_HASH "none")
endif()

# Only write if the Hash (the Git Identity) is different
if(NOT "${NEW_HASH}" STREQUAL "${OLD_HASH}")
    file(WRITE "${META_FILE}" "${NEW_META}")
else()
    # We DON'T write. The old file stays on disk with its old timestamp.
    # This prevents all subprojects from thinking they need to re-link.
endif()

# 4. Add to the argument list for subprojects
set(METADATA_DEFS -DSYSTEM_METADATA_JSON_PATH=${META_FILE})

# ---------------------------------------------------------------------------------------------
message(STATUS "Adding the 'ecu' project")

set(ECU_CMAKE_ARG_LIST
    -DLIB_DIR=${LIB_DIR}
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_SOURCE_DIR}/cmake/toolchains/host.cmake
    ${METADATA_DEFS}
    -DPython3_EXECUTABLE=${Python3_EXECUTABLE}
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

# ---------------------------------------------------------------------------------------------
message(STATUS "Adding the 'EP' project")

set(EP_CMAKE_ARG_LIST
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_SOURCE_DIR}/cmake/toolchains/arm-none-eabi.cmake
    -DLIB_DIR=${LIB_DIR}
    -DEPROM_LIB_SRC_DIR=${CMAKE_SOURCE_DIR}/eprom_lib
    -DECU_BINARY_DIR=${CMAKE_BINARY_DIR}/ecu
    -DPICO_SDK_PATH=${PICO_SDK_PATH}
    -DPICO_EXAMPLES_PATH=${PICO_EXAMPLES_PATH}
    -DCMAKE_EXPORT_COMPILE_COMMANDS=1
    -DTOOLS_SOURCE_DIR=${TOOLS_SOURCE_DIR}
    -DPython3_EXECUTABLE=${Python3_EXECUTABLE}
    -DPYTHON_SITEPACKAGES=${VENV_PATH}/lib/python${PYTHON_VERSION}/site-packages
)

ExternalProject_Add(EP
    DEPENDS tools ecu picotool
    PREFIX ${CMAKE_BINARY_DIR}/EP
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/EP
    BINARY_DIR ${CMAKE_BINARY_DIR}/EP
    CMAKE_GENERATOR ${generator}
    CMAKE_ARGS -DCMAKE_BUILD_TYPE:STRING=Debug ${EP_CMAKE_ARG_LIST}
    BUILD_ALWAYS true
    INSTALL_COMMAND ""
)

# ---------------------------------------------------------------------------------------------
message(STATUS "Adding the 'SwdReflash' project")

set(SWDREFLASH_CMAKE_ARG_LIST
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_SOURCE_DIR}/cmake/toolchains/arm-none-eabi.cmake
    -DPICO_SDK_PATH=${PICO_SDK_PATH}
    -DPICO_EXAMPLES_PATH=${PICO_EXAMPLES_PATH}
    -DCMAKE_EXPORT_COMPILE_COMMANDS=1
)

ExternalProject_Add(SwdReflash
    # No dependencies - SwdReflash is completely standalone
    PREFIX ${CMAKE_BINARY_DIR}/SwdReflash
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/SwdReflash
    BINARY_DIR ${CMAKE_BINARY_DIR}/SwdReflash
    CMAKE_GENERATOR ${generator}
    CMAKE_ARGS -DCMAKE_BUILD_TYPE:STRING=Debug ${SWDREFLASH_CMAKE_ARG_LIST}
    BUILD_ALWAYS true
    INSTALL_COMMAND ""
)

# ---------------------------------------------------------------------------------------------
message(STATUS "Adding the 'WpUsbBoot' project")

set(WPUSBBOOT_CMAKE_ARG_LIST
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_SOURCE_DIR}/cmake/toolchains/arm-none-eabi.cmake
    -DPICO_SDK_PATH=${PICO_SDK_PATH}
)

ExternalProject_Add(WpUsbBoot
    # No dependencies - WpUsbBoot is completely standalone
    PREFIX ${CMAKE_BINARY_DIR}/WpUsbBoot
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/WpUsbBoot
    BINARY_DIR ${CMAKE_BINARY_DIR}/WpUsbBoot
    CMAKE_GENERATOR ${generator}
    CMAKE_ARGS -DCMAKE_BUILD_TYPE:STRING=Debug ${WPUSBBOOT_CMAKE_ARG_LIST}
    BUILD_ALWAYS true
    INSTALL_COMMAND ""
)

# ---------------------------------------------------------------------------------------------
message(STATUS "Adding the 'ap_proxy' project")

# ap_proxy manages its own toolchain setup via PICO_TOOLCHAIN_PATH auto-detection
# (same pattern as the standalone build, just driven by the superbuild).
set(PROXY_PICO_CMAKE_ARG_LIST
    -DPICO_SDK_PATH=${PICO_SDK_PATH}
    -DCMAKE_EXPORT_COMPILE_COMMANDS=1
)

ExternalProject_Add(ap_proxy
    # No dependencies - ap_proxy is completely standalone
    PREFIX ${CMAKE_BINARY_DIR}/ap_proxy
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/tools/ap_proxy
    BINARY_DIR ${CMAKE_BINARY_DIR}/ap_proxy
    CMAKE_GENERATOR ${generator}
    CMAKE_ARGS -DCMAKE_BUILD_TYPE:STRING=Debug ${PROXY_PICO_CMAKE_ARG_LIST}
    BUILD_ALWAYS true
    INSTALL_COMMAND ""
)

# ---------------------------------------------------------------------------------------------
message(STATUS "Adding the 'WP' project")

set(WP_CMAKE_ARG_LIST
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_SOURCE_DIR}/cmake/toolchains/arm-none-eabi.cmake
    -DLIB_DIR=${LIB_DIR}
    -DPICO_SDK_PATH=${PICO_SDK_PATH}
    -DPICO_EXAMPLES_PATH=${PICO_EXAMPLES_PATH}
    -DFREERTOS_KERNEL_PATH=${FREERTOS_KERNEL_PATH}
    -DLFS_SRC_DIR=${LFS_SRC_DIR}
    -DCMAKE_EXPORT_COMPILE_COMMANDS=1
    -DPython3_EXECUTABLE=${Python3_EXECUTABLE}
)

ExternalProject_Add(WP
    DEPENDS tools EP SwdReflash picotool
    PREFIX ${CMAKE_BINARY_DIR}/WP
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/WP
    BINARY_DIR ${CMAKE_BINARY_DIR}/WP
    CMAKE_GENERATOR ${generator}
    CMAKE_ARGS -DCMAKE_BUILD_TYPE:STRING=Debug ${WP_CMAKE_ARG_LIST}
    BUILD_ALWAYS true
    INSTALL_COMMAND ""
)

# ---------------------------------------------------------------------------------------------
# To make intellesense work better in the context of multiple external projects, we need to create
# a single database that covers everything.
if(CMAKE_EXPORT_COMPILE_COMMANDS)

    # Ensure a valid (empty) compilation database exists at configure time
    file(WRITE ${CMAKE_BINARY_DIR}/compile_commands.json "[]")

    set(MERGE_SCRIPT ${CMAKE_BINARY_DIR}/merge_compile_commands.sh)

    file(WRITE ${MERGE_SCRIPT} "
#!/bin/sh
set -e

MERGED_DB=\"${CMAKE_BINARY_DIR}/compile_commands.json\"
TMP_DB=\"${CMAKE_BINARY_DIR}/compile_commands.tmp.json\"

INPUTS=\"\"

for f in \
  \"${CMAKE_BINARY_DIR}/WP/compile_commands.json\" \
  \"${CMAKE_BINARY_DIR}/EP/compile_commands.json\" \
  \"${CMAKE_BINARY_DIR}/SwdReflash/compile_commands.json\"
do
  if [ -s \"$f\" ]; then
    INPUTS=\"$INPUTS $f\"
  fi
done

if [ -n \"$INPUTS\" ]; then
  jq -s 'add' $INPUTS > \"$TMP_DB\"
  mv \"$TMP_DB\" \"$MERGED_DB\"
else
  echo \"[]\" > \"$MERGED_DB\"
fi
")



    file(CHMOD ${MERGE_SCRIPT}
         PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                     GROUP_READ GROUP_EXECUTE
                     WORLD_READ WORLD_EXECUTE)

    add_custom_target(merge_compile_commands ALL
        COMMAND ${MERGE_SCRIPT}
        COMMENT "Merging compile command databases for IntelliSense"
    )

    # Ensure merge runs AFTER these builds
    add_dependencies(merge_compile_commands WP EP SwdReflash)

endif()

