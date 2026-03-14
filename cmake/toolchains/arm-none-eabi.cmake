set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Define the base path outside the IF block so it's always available
set(ARM_TOOLCHAIN_BASE "/opt/arm/arm-none-eabi")

# Uncomment the following line to MANUALLY OVERRIDE the automatic search that follows
# set(ARM_NONE_EABI_VERSION "14.2.rel1")

# Search for the most recent version of ARM tools
if(NOT ARM_NONE_EABI_VERSION)
    file(GLOB version_dirs RELATIVE "${ARM_TOOLCHAIN_BASE}" "${ARM_TOOLCHAIN_BASE}/*")

    if(version_dirs)
        list(SORT version_dirs ORDER DESCENDING)
        list(GET version_dirs 0 detected_version)
        set(ARM_NONE_EABI_VERSION "${detected_version}")
        message(STATUS "Auto-detected latest ARM toolchain: ${ARM_NONE_EABI_VERSION}")
    else()
        message(FATAL_ERROR "No ARM toolchains found in ${ARM_TOOLCHAIN_BASE}")
    endif()
endif()

# Create a symlink in the build tree that points directly to the ARM tools /bin folder
set(TOOLCHAIN_LINK "${CMAKE_BINARY_DIR}/arm_toolchain_latest")

execute_process(
    COMMAND ${CMAKE_COMMAND} -E create_symlink
    "${ARM_TOOLCHAIN_BASE}/${ARM_NONE_EABI_VERSION}/bin"
    "${TOOLCHAIN_LINK}"
)

set(CROSSCOMPILE_TOOL_PATH "${TOOLCHAIN_LINK}")

# Standard Toolchain Definitions
set(CMAKE_C_COMPILER ${CROSSCOMPILE_TOOL_PATH}/arm-none-eabi-gcc CACHE FILEPATH "C compiler")
set(CMAKE_CXX_COMPILER ${CROSSCOMPILE_TOOL_PATH}/arm-none-eabi-g++ CACHE FILEPATH "C++ compiler")
set(CMAKE_C_OUTPUT_EXTENSION .o)

set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER} CACHE FILEPATH "ASM compiler")
set(CMAKE_ASM_COMPILE_OBJECT "<CMAKE_ASM_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT> -c <SOURCE>")
set(CMAKE_INCLUDE_FLAG_ASM "-I")

set(CMAKE_OBJCOPY ${CROSSCOMPILE_TOOL_PATH}/arm-none-eabi-objcopy CACHE FILEPATH "")
set(CMAKE_OBJDUMP ${CROSSCOMPILE_TOOL_PATH}/arm-none-eabi-objdump CACHE FILEPATH "")

# Disable compiler checks
set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)