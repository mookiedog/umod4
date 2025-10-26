cmake_minimum_required(VERSION 3.13)

# This CMakeLists.txt builds the tools required by other parts of this project.
# All tools are compiled to run on the host's OS.
#
# This project is meant to be included as a CMake ExternalProject. The CMakeLists.txt
# that adds this ExternalProject must define the following variables:
#   LIB_DIR (the path to the directory containing the shared source files)
#   TOOLS_INSTALL_DIR (the path to where the tools should be installed after getting built by this file)
#   VENV_PATH (the path to the virtual environment where packages should be installed)

if (NOT DEFINED LIB_DIR)
    message(FATAL ": Variable LIB_DIR must be defined!")
endif()

if (NOT DEFINED TOOLS_INSTALL_DIR)
    message(FATAL ": Variable TOOLS_INSTALL_DIR must be defined!")
endif()

if (NOT DEFINED VENV_PATH)
    message(FATAL_ERROR ": Variable VENV_PATH must be defined!")
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


# Find Python
find_package(Python3 COMPONENTS Interpreter REQUIRED)


####################################################################################################
# This section creates the log_sym.py source code.

set(SOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/inc/log_base.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../ecu/src/ECU_log.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../EP/src/EP_log.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../WP/src/WP_log.h
)

# The C header to Python constant converter script:
set(PYTHON_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/src/h2py.py)

# Define the name of the file that will hold the log symbol class that we will generate
set(LOG_CLASS_NAME Logsyms)
set(LOG_CLASS_FILE ${LOG_CLASS_NAME}.py)

# Define package directory structure
set(PACKAGE_DIR ${CMAKE_BINARY_DIR}/python_packages/Logsyms/src)
set(LOGSYMS_PY_FILE ${PACKAGE_DIR}/${LOG_CLASS_FILE})
set(INIT_PY_FILE ${PACKAGE_DIR}/__init__.py)

# Create the directory structure first
add_custom_command(
    OUTPUT ${PACKAGE_DIR}/.dir_created
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PACKAGE_DIR}
    COMMAND ${CMAKE_COMMAND} -E touch ${PACKAGE_DIR}/.dir_created
    COMMENT "Creating package directory structure"
    VERBATIM
)

# Generate the Logsyms.py file directly into the package directory
add_custom_command(
    OUTPUT ${LOGSYMS_PY_FILE}
    DEPENDS ${SOURCE_FILES} ${PYTHON_SCRIPT} ${CMAKE_CURRENT_LIST_FILE} ${PACKAGE_DIR}/.dir_created
    COMMENT "Generating Logsyms.py directly into package directory"
    COMMAND ${Python3_EXECUTABLE} ${PYTHON_SCRIPT} --class-name Logsyms -o ${LOGSYMS_PY_FILE} ${SOURCE_FILES}
    VERBATIM
)

# Create the __init__.py file with proper content
add_custom_command(
    OUTPUT ${INIT_PY_FILE}
    DEPENDS ${LOGSYMS_PY_FILE}
    COMMENT "Creating __init__.py file with package-level imports"
    COMMAND ${CMAKE_COMMAND} -E echo "# Import everything from Logsyms.py so it's available at package level" > ${INIT_PY_FILE}
    COMMAND ${CMAKE_COMMAND} -E echo "from .Logsyms import *" >> ${INIT_PY_FILE}
    VERBATIM
)

# Create a custom target that depends on all the package files
add_custom_target(update_log_symbols ALL
    DEPENDS ${LOGSYMS_PY_FILE} ${INIT_PY_FILE}
)

####################################################################################################
# Automatic installation logic - install whenever Logsyms.py gets updated

# Check if virtual environment exists and set Python executable accordingly
if(EXISTS ${VENV_PATH})
    message(STATUS "Virtual environment found at ${VENV_PATH}")
    set(Python3_EXECUTABLE "${VENV_PATH}/bin/python3")
    message(STATUS "Using Python executable: ${Python3_EXECUTABLE}")
else()
    message(FATAL_ERROR "No virtual environment found at ${VENV_PATH}")
endif()

# Get the Python version from the virtual environment
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

# Add installation command that only runs when the package files change
# This approach should work without creating circular dependencies
add_custom_command(
    TARGET update_log_symbols
    COMMAND ${CMAKE_COMMAND} -E make_directory ${VENV_PATH}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${VENV_PATH}/lib/python${PYTHON_VERSION}/site-packages/Logsyms
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LOGSYMS_PY_FILE} ${VENV_PATH}/lib/python${PYTHON_VERSION}/site-packages/Logsyms/
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${INIT_PY_FILE} ${VENV_PATH}/lib/python${PYTHON_VERSION}/site-packages/Logsyms/
    COMMENT "Installing Logsyms package to virtual environment (Python ${PYTHON_VERSION})"
    VERBATIM
)
