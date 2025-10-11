# This toolchain file can be used to drive a cross compiler targeting an ARM Cortex M0+
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Specify the exact release of the cross-compilation tools we want to use...
set(ARM_NONE_EABI_VERSION "14.2.rel1")

# ...as well as where to find the them
set(CROSSCOMPILE_TOOL_PATH "/opt/arm/arm-none-eabi/${ARM_NONE_EABI_VERSION}/bin")

set(CMAKE_C_COMPILER ${CROSSCOMPILE_TOOL_PATH}/arm-none-eabi-gcc CACHE FILEPATH "C compiler")
set(CMAKE_CXX_COMPILER ${CROSSCOMPILE_TOOL_PATH}/arm-none-eabi-g++ CACHE FILEPATH "C++ compiler")
set(CMAKE_C_OUTPUT_EXTENSION .o)

set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER} CACHE FILEPATH "ASM compiler")
set(CMAKE_ASM_COMPILE_OBJECT "<CMAKE_ASM_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT>   -c <SOURCE>")
set(CMAKE_INCLUDE_FLAG_ASM "-I")

set(CMAKE_OBJCOPY ${CROSSCOMPILE_TOOL_PATH}/arm-none-eabi-objcopy CACHE FILEPATH "")
set(CMAKE_OBJDUMP ${CROSSCOMPILE_TOOL_PATH}/arm-none-eabi-objdump CACHE FILEPATH "")

# Disable compiler checks.
set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
