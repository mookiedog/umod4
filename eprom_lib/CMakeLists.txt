cmake_minimum_required(VERSION 3.13)

# set (CMAKE_VERBOSE_MAKEFILE ON)

project(EPROM_LIB)

set(EPROM_NAMES
# These are the pre-RP58 codebases
#  Sport854
#  RsvTiEvoUsa9901
#  A63
#  P08
#
# From here on are the RP58-compatible codebases.
#
# All builds get the datalogging EPROM UM4.bin include into their build.
# To include any of the other RP58-compatible EPROMs into the build,
# you need to have a copy of the .bin file named exactly as below, such as "549EuroC.bin".
# Put your properly named .bin files into the eprom_lib/bin_files directory,
# then uncomment the appropriate name[s], below:
  RP58
  PA59
  549USA
  Edwards
  549EuroC
  549EuroKatD
  8796505
  8796529
  8796539
)

# Define how to construct the .bson and .c files from the .dsc & .bin files:
foreach(EPROM_ID ${EPROM_NAMES})
  # message(STATUS "Adding ${EPROM_ID}.bson to the build")

  # Double -dash -wrapper needed until I delete the old image processing stuff. It
  # is generating the same wrapper names!
  add_custom_target(${EPROM_ID}--wrapper ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${EPROM_ID}.bson
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${EPROM_ID}.c
    DEPENDS ${TOOLS_SOURCE_DIR}/bin-to-bson.py
  )

  set(DSC_PATH ${CMAKE_CURRENT_SOURCE_DIR}/src/${EPROM_ID}.dsc)
  set(BIN_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin_files/${EPROM_ID}.bin)

  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${EPROM_ID}.bson ${CMAKE_CURRENT_BINARY_DIR}/${EPROM_ID}.c
    DEPENDS ${TOOLS_SOURCE_DIR}/bin-to-bson.py
    DEPENDS ${DSC_PATH}
    DEPENDS ${BIN_PATH}
    VERBATIM
    COMMAND ${Python3_EXECUTABLE} ${TOOLS_SOURCE_DIR}/bin-to-bson.py -d ${DSC_PATH} -b ${BIN_PATH} -o ${CMAKE_CURRENT_BINARY_DIR}/${EPROM_ID}.bson -j ${CMAKE_CURRENT_BINARY_DIR}/${EPROM_ID}.json -c ${CMAKE_CURRENT_BINARY_DIR}/${EPROM_ID}.c
  )
endforeach()
