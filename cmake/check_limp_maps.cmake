# Build-time guard for the limp mode maps file (eprom_lib/bin_files/LIMP_MODE_MAPS).
#
# The limp mode image is UM4 code plus the maps from that file.  Maps are only usable with the UM4
# code if the file is an unprotected image with the stock RP58 code base: a scrambled (protected)
# image, or one from a different code base, would give limp mode maps that the UM4 code cannot use.
# Both fail this check: the murmur3 checksum of the code range (0x4000-0x7FFF) of an unprotected,
# RP58-compatible image is always 0x18E7E9F2.  See eprom_lib/README.md.
#
# This runs at build time only.  At run time the EP deliberately does no such checking: it copies
# whatever image it has and hopes that it runs well enough to get the rider home.
#
# Usage: cmake -DMAPS_FILE=<file> -P check_limp_maps.cmake

set(EXPECTED_CHECKSUM "0x18E7E9F2")

if(NOT MAPS_FILE)
    message(FATAL_ERROR "check_limp_maps.cmake: MAPS_FILE was not given")
endif()

find_program(CHECKSUM_EXE checksum HINTS "$ENV{HOME}/.local/bin")
if(NOT CHECKSUM_EXE)
    message(FATAL_ERROR "The 'checksum' tool was not found (the tools project installs it into ~/.local/bin). "
        "It is needed to check the limp mode maps file.")
endif()

execute_process(
    COMMAND ${CHECKSUM_EXE} -o 0x4000 -3 "${MAPS_FILE}"
    OUTPUT_VARIABLE ACTUAL_CHECKSUM
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE CHECKSUM_RESULT)
if(NOT CHECKSUM_RESULT EQUAL 0)
    message(FATAL_ERROR "'checksum' failed on ${MAPS_FILE}")
endif()

string(TOUPPER "${ACTUAL_CHECKSUM}" ACTUAL_UPPER)
string(TOUPPER "${EXPECTED_CHECKSUM}" EXPECTED_UPPER)
if(NOT ACTUAL_UPPER STREQUAL EXPECTED_UPPER)
    get_filename_component(MAPS_NAME "${MAPS_FILE}" NAME)
    message(FATAL_ERROR
        "\n"
        "  eprom_lib/bin_files/LIMP_MODE_MAPS points at ${MAPS_NAME}, which is not an unprotected,\n"
        "  RP58-compatible image: the checksum of its code range (0x4000-0x7FFF) is ${ACTUAL_CHECKSUM},\n"
        "  but it must be ${EXPECTED_CHECKSUM}.\n"
        "\n"
        "  A protected (scrambled) image, or one from a different code base, would give limp mode\n"
        "  maps that the UM4 code cannot use.  See the table in eprom_lib/README.md, and point the\n"
        "  link at an unprotected image marked RP58-compatible, or remove the link.\n")
endif()
