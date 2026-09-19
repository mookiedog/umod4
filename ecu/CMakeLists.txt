cmake_minimum_required(VERSION 3.13)

project(UM4)

# We use a gcc compiler strictly as a preprocessor in this project.
# Any gcc target will do because we only run the preprocessing phase, so we
# use the one referenced in the host toolchain file:
set (PREPROCESSOR ${CMAKE_C_COMPILER})

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/${PROJECT_NAME}.S)

# Build one ECU image from UM4.S. The first argument is the image name (output files are
# <name>.s/.o/.elf/.lst/.s19/.bin in the build dir). Any further arguments are preprocessor
# symbols to define for that image (e.g. BENCH_IGNITION_TEST).
function(add_ecu_image IMAGE)
  set(NAME ${CMAKE_CURRENT_BINARY_DIR}/${IMAGE})

  set(DEFS "")
  foreach(SYM ${ARGN})
    list(APPEND DEFS -D${SYM})
  endforeach()

  set(OUTPUTS
    ${NAME}.s
    ${NAME}.o
    ${NAME}.elf
    ${NAME}.lst
    ${NAME}.s19
    ${NAME}.bin
  )
  add_custom_target(${IMAGE} ALL
    DEPENDS
      ${OUTPUTS}
  )

  add_custom_command(
    OUTPUT
      ${OUTPUTS}
    DEPENDS
      ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
      ${SRC}
      ${LIB_DIR}/inc/log_ids.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/custom_eprom.ld
      ${SYSTEM_METADATA_JSON_PATH}

    COMMENT "Building and Patching ${IMAGE}.bin"
    VERBATIM
      # 1. Preprocess
      COMMAND ${PREPROCESSOR} -E ${DEFS} -I${LIB_DIR}/inc ${SRC} > ${NAME}.s

      # 2. Assemble
      COMMAND m68hc11-elf-as -m68hc11 --short-branchs --print-insn-syntax ${NAME}.s -o ${NAME}.o -a=${NAME}.lst

      # 3. Link
      COMMAND m68hc11-elf-ld -T ${CMAKE_CURRENT_SOURCE_DIR}/src/custom_eprom.ld -o ${NAME}.elf ${NAME}.o

      # 4. Generate S19
      COMMAND m68hc11-elf-objcopy --srec-len 32 -I elf32-m68hc11 -O srec ${NAME}.elf ${NAME}.s19

      # 5. Generate Binary
      COMMAND m68hc11-elf-objcopy ${NAME}.elf --output-target=binary ${NAME}.bin

      # 6. Patch build metadata into binary
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/patch_metadata.py"
              "${SYSTEM_METADATA_JSON_PATH}"
              "${NAME}.elf"
              "${NAME}.bin"
  )
endfunction()

# The normal data-logging ECU image: UM4.bin
add_ecu_image(${PROJECT_NAME})

# 'sparky': bench-only test image that skips the real mainloop and fires both ignition coils
# as though the engine were idling at 1200 RPM.
# It can be loaded by the EP like any other .bin file, or it can be flashed into
# a 29C256 flash chip and inserted directly into an ECU.
# The 'sparky.bin' file gets generated to build/ecu just like UM4.bin, above.
add_ecu_image(sparky BENCH_IGNITION_TEST)
