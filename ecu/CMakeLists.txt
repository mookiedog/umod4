cmake_minimum_required(VERSION 3.13)

project(UM4)

# We use a gcc compiler strictly as a preprocessor in this project.
# Any gcc target will do because we only run the preprocessing phase, so we
# use the one referenced in the host toolchain file:
set (PREPROCESSOR ${CMAKE_C_COMPILER})

set(NAME ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME})

set(OUTPUTS
  ${NAME}.s
  ${NAME}.o
  ${NAME}.elf
  ${NAME}.lst
  ${NAME}.s19
  ${NAME}.bin
  ${NAME}.json
  ${NAME}.bson
  ${NAME}.c
)
add_custom_target(${PROJECT_NAME} ALL
  DEPENDS
    ${OUTPUTS}
)

add_custom_command(
  OUTPUT
    ${OUTPUTS}
  DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/src/${PROJECT_NAME}.S
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ECU_log.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/custom_eprom.ld
    ${TOOLS_SOURCE_DIR}/bin-to-bson.py

  COMMENT "Building and Patching UM4.bin"
  VERBATIM
    # 1. Preprocess
    COMMAND ${PREPROCESSOR} -E -I${LIB_DIR}/inc ${CMAKE_CURRENT_SOURCE_DIR}/src/${PROJECT_NAME}.S > ${NAME}.s

    # 2. Assemble
    COMMAND m68hc11-elf-as -m68hc11 --short-branchs --print-insn-syntax ${NAME}.s -o ${NAME}.o -a=${NAME}.lst

    # 3. Link
    COMMAND m68hc11-elf-ld -T ${CMAKE_CURRENT_SOURCE_DIR}/src/custom_eprom.ld -o ${NAME}.elf ${NAME}.o

    # 4. Generate S19
    COMMAND m68hc11-elf-objcopy --srec-len 32 -I elf32-m68hc11 -O srec ${NAME}.elf ${NAME}.s19

    # 5. Generate Binary
    COMMAND m68hc11-elf-objcopy ${NAME}.elf --output-target=binary ${NAME}.bin

    # 6. Generate BSON/JSON
    COMMAND ${Python3_EXECUTABLE} ${TOOLS_SOURCE_DIR}/bin-to-bson.py
            -d ${CMAKE_CURRENT_SOURCE_DIR}/src/${PROJECT_NAME}.dsc
            -b ${NAME}.bin -o ${NAME}.bson -j ${NAME}.json -c ${NAME}.c
)
