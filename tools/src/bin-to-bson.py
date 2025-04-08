# Convert a .dsc EPROM description file and its associated .bin file into a
# BSON object that combines the description with the binary data.
#
# A .dsc description file is defined to be formatted as "JSON with comments".
# At the moment, the .dsc file only contains string values for the key/value pairs.
#
# The process goes like this:
#   1. The .dsc file gets minified() to remove comments
#   2. The minified JSON file is loaded into Python as a python object
#   3. Various fields are added to the python object, such as:
#       - the EPROM memory image gets added as a python byte array
#       - memory offset and length information for the image
#       - fields that get added are allowed to be data types other than strings:
#           - integers for checksums, start offsets, lengths, etc.
#           - byte arrays for images
#         This minimizes the amount of work required for the EP to parse a BSON object
#   4. For RP58-compatible builds, a mem object is created that defines
#      where to find the mapblob in the EPROM image array
#   5. For UM4 builds, a mem object is created to explain where to find the codeblob
#
# When everything is added, the bson library is used to convert the object from its Python
# representation to a BSON representation.
#
# The BSON representation can be emitted as C source code for inclusion into umod4.
# This is not a long-term solution, but it will make it easy to get BSON into
# EP builds without disrupting the existing system.


import sys
import json
import argparse
import mmh3

# Simplified Memory Map: see EP/src/memorymap.h for the definitive data
#    $8000..$9BFF ( 7168 bytes)  Monolithic map data blob, rounded up past its actual end at $9BD8
#    $9C00..$9FDF (  992 bytes)  Unused, set to $3F
#    $9FE0..$9FFF (   32 bytes)  diag code table, rounded from 27 bytes up to 32
#    $A000..$BFFF ( 8192 bytes)  Unused, set to $3F
#    $C000..$FFFF (16384 bytes)  Code space. First instruction is actually at $C003.
#                                Note that $C000..$C002 will be 'UM4' for UM4 builds, $3F for all others.
# Define the EPROM offsets for these symbols:
mapStartOffset=0x0000
mapLength=0x1C00
diagStartOffset=0x1FE0
diagLength=0x20

# PyMongo's bson library
from bson import BSON

# Used to strip comments from the .dsc (description) files
from json_minify import json_minify

# Descrambling support is private. See if this system supports it
try:
    import descramble
except:
    # Descrambler support will not be available
    def descramble(scrambledImage):
        return False, b'';


def fatal(error_msg):
    global progname
    print(f"{progname}: {error_msg}", file=sys.stderr)
    exit(1)

def parse_args():
    parser = argparse.ArgumentParser(
        description=str(
            'Given an EPROM image and a JSON descriptor file that provides metainformation for that image,\n'
            'generate a JSON file that merges the descriptor information with the binary data from the image.\n'
        )
    )

    global progname
    progname = parser.prog

    parser.add_argument("-v", "--verbose", help="Display verbose output",
                        action="store_true")


    parser.add_argument("-d", "--descriptorfile", required=True, help="path to JSON binfile descriptor for the EPROM")
    #parser.add_argument('--image-file', type=argparse.FileType('r'), _StoreAction(option_strings=['--image-file'], dest='bin_file_path', nargs=None, const=None, default=None, type=FileType('r'), choices=None, help=None, metavar=None))

    parser.add_argument("-b", "--binfile", required=True, help="path to EPROM .bin file")

    parser.add_argument("-o", "--outfile", required=True, help="output path for generated BSON data")

    parser.add_argument("-c", "--csource", required=False, help="output path for generated C source")

    parser.add_argument("-j", "--json", required=False, help="output path for generated JSON")

    args = parser.parse_args()

    if (args.verbose):
        print("Descriptor path:", args.descriptorfile)
        print("Bin path:", args.binfile)
        print("BSON Output path:", args.outfile)
        print("Csource path:", args.csource)
        print("Json output path:", args.json)

    return args


# Read the entire descriptor file into a string variable
def read_descriptor_file(filename):
    # The file is expected to be formatted as UTF8 JSON, with comments.
    try:
        dfile=open(args.descriptorfile, mode="r")
        text = dfile.read()
    except:
        fatal(f"Unable to open descriptor file: <{filename}>")
        exit(1)

    dfile.close
    return text

# Read the EPROM .bin file into a byte array
def read_bin_file(filename):
    try:
        f = open(filename, mode="rb")
        bindata = f.read()
    except:
        fatal(f"Unable to open bin file <{filename}>")

    f.close()

    # Verify that the EPROM .bin file is the proper length
    if len(bindata) != 32768:
        fatal(f"The bin file <{filename}> must be exactly 32768 bytes long, saw {len(bindata)}")

    return bindata

# Create a python DICT that represents the contents of the original JSON descriptor text file.
# The file is expected to be formatted as UTF8 JSON, with comments.
def process_descriptor(filename):
    try:
        dfile=open(args.descriptorfile, mode="r")
        dsc_text = dfile.read()
    except:
        fatal(f"Unable to open descriptor file: <{filename}>")
        exit(1)

    dfile.close

    # Use json_minify to strip comments from the original descriptor file.
    # Load the minified JSON file into 'd',  python DICT object.
    # If the JSON has any errors, json.loads() will fail
    try:
        d = json.loads(json_minify(dsc_text))
    except ValueError as error:
        fatal(f"Descriptor file {args.descriptorfile} is not correct JSON syntax")

    # If we get here, there were no JSON syntax errors.
    return d


def validate_descriptor(filename, dsc):
    # Part1: Error check the python dict parameter 'dsc' to make sure that it contains all the required keys
    errmsg = f'Description file error in file {filename}:'

    if "eprom" not in dsc:
        fatal(f'{errmsg} "eprom" key is missing')

    if "name" not in dsc["eprom"]:
        fatal(f'{errmsg} "eprom" value must define a "name" key')

    if "info" not in dsc["eprom"]:
        fatal(f'{errmsg} "eprom" value needs an "info" key')

    if "maps" not in dsc["eprom"]:
        fatal(f'{errmsg} "eprom" must contain a "maps" key')

    if "info" not in dsc["eprom"]["maps"]:
        fatal(f'{errmsg} "eprom" "maps" must contain a "info" key')

    if "code" in dsc["eprom"]:
        fatal(f'{errmsg} "eprom" must not contain a "code" key')

    # Part2: Make sure that the dict does NOT contain certain keys because we
    # will be generating them automatically:
    if "image" in descriptor["eprom"]:
        fatal(f'{errmsg} "eprom" must not contain an "image" key: it will be generated automatically')

    if "mem" in descriptor["eprom"]["maps"]:
        fatal(f'{errmsg} eprom.maps must not contain a "mem" key: it will be generated automatically')

# Generate a BSON document corresponding to the dsc_dict DICT parameter into file 'outfile'
def generate_bson(dsc_dict, outfile):
    try:
        b = BSON.encode(descriptor)
    except:
        fatal("Error converting from python to BSON")

    # print ("bson encoded data:", b)
    if args.verbose:
        print(f"size of bson document {len(b)}")

    # Write our BSON object to the output file.
    # It gets written as a BSON document, meaning that the length of the document
    # is encoded in the first 4 bytes, followed by the document contents.
    if args.verbose:
        print(f"writing bson object to <{outfile}>")

    try:
        ofile = open(outfile, mode="wb")
    except:
        fatal(f"Unable to open output file <{outfile}>")

    try:
        ofile.write(b);
    except:
        fatal(f"Unable to write output file <{outfile}>")

    ofile.close()

    return b

# Generate a JSON representation of our eprom's DICT object into file 'filename'.
# The eprom.mem.bin binary data is stripped out to make the output readable.
def generate_json(d, filename):
    # Write the modified JSON, as a debug aid to see what the BSON looks like
    if args.verbose:
        print(f"writing json object to <{filename}>")
    try:
        jfile=open(filename, mode="w")
    except:
        fatal(f"Unable to open JSON output file <{filename}>")

    # Delete the binary data field before we convert it to JSON
    d["eprom"]["mem"]["bin"] = {}

    jstr = json.dumps(d, indent=4)

    try:
        print(jstr, file=jfile)
    except:
        fatal(f"Unable to write JSON output file <{filename}>")

    jfile.close()

# Generate C source code representing the BSON object 'b' as an initialized array of bytes.
# The array gets placed in a special linker section '.bsonStore'.
def generate_c_source(d, b, filename):
    if args.verbose:
        print("Generating C source")

    try:
        cs = open(filename, mode="w")
    except:
        fatal(f"Unable to open csource output file for writing <{filename}>")

    print(f'/* WARNING: Do Not Edit - Automatically generated source code!', file=cs)
    print(f' *', file=cs)
    print(f' * Eprom name: {d["eprom"]["name"]}', file=cs)
    print(f' */', file=cs)
    print(f'\n#include <stdint.h>', file=cs)
    print(file=cs)

    bsonDocLen = len(b)
    print(f'uint8_t eprom_{d["eprom"]["name"]}_bson[{bsonDocLen}] __attribute__((section(".bsonStore"))) = {{', file=cs)
    for i in range(bsonDocLen):
        byte = b[i]
        if (i&0xF==0):
            print(f'  /* {i:04X} */ ', end="", file=cs)
            chars=""
        print(f'0x{byte:02x}',end="", file=cs)
        # For readability: if a byte is a printable ASCII character, arrange to get it added at the end of the source line
        # Do not print '\' chars to avoid the generated C source from containing what a compiler would interpret as '\' escaped special character
        if (byte>ord(" ") and (byte<0x7F) and (byte!=ord('\\'))):
            chars += chr(byte)
        else:
            chars += "."
        if (i < (bsonDocLen-1)):
            print(f',', end="", file=cs)
        if (i == bsonDocLen-1) or ((i & 0xF) == 0xF):
            print(f'    // {chars}', file=cs)
    print("};", file=cs)

    print('\n/* Verify that the size of the original BSON doc matches the size of the generated array of data */', file=cs)
    print(f'_Static_assert ({bsonDocLen} == sizeof(eprom_{d["eprom"]["name"]}_bson), "Internal error: Expected size != actual compiled size!");', file=cs)
    cs.close

###################################################################################################
# The main program

args = parse_args()

# Read the bin file into 'image' as a byte array
image = read_bin_file(args.binfile)

# Read and process the JSON/descriptor file text into 'descriptor', a Python DICT object
descriptor = process_descriptor(args.descriptorfile)

# Error-check the descriptor to make sure it is valid
validate_descriptor(args.descriptorfile, descriptor)

# Daughterboard key is optional in the descriptor file (defaults to no daughterboard needed),
# but will always present in the output file:
daughterboard=False

# If a daughterboard key exists, make sure that its value is something we know about
if "daughterboard" not in descriptor["eprom"]:
    descriptor["eprom"]["daughterboard"] = "N"
else:
    db_style=descriptor["eprom"]["daughterboard"]
    match db_style:
        case "N":
            daughterboard=False
        case "A":
            # The standard Aprilia daughterboard. Rumor is that there was more than one
            # style of daughterboard but apparently, they are rare.
            daughterboard=True
        case _:
            fatal(f'"daughterboard" value must be one of "N" (no daughterboard required), "A" (Standard Aprilia V1); saw "{db_style}"')

# Error checking is complete.
# Now we start merging the .bin file data into our data structure,
# calculating some things as we go.

# Calculate the M3 hash over the entire eprom image.
# Store that M3 hash in the eprom's "mem" object.
# The hash can be used to identify a specific EPROM, or to verify
# that the binary contents have not been corrupted.
eprom_m3hash = 0xFFFFFFFF & mmh3.hash(image, 0xFFFFFFFF, signed=False)
if args.verbose:
    print(f"M3 hash of the entire image: {hex(eprom_m3hash)}")

# Add a "mem" object to the "eprom" section of the description.
# It will represent the entire EPROM image.
descriptor["eprom"]["mem"] = {}
descriptor["eprom"]["mem"]["startOffset"] = 0x0000
descriptor["eprom"]["mem"]["length"] = 0x8000
descriptor["eprom"]["mem"]["m3"] = eprom_m3hash

# Add the binary data taken from the .bin file to our descriptor object
# as a binary byte array.
# Note: EPROM .bin files for EPROMs that require a daughterboard are assumed to be
# stored in their original, scrambled format as if the EPROM had been removed
# from the daughterboard before being read on an EPROM reader.
descriptor["eprom"]["mem"]["bin"] = image

if args.verbose:
    print("Testing to see if the image is RP58-compatible")

# Check the first 3 bytes in the code area to see if this is a UM4 build
if ((image[0x4000] == ord('U')) and (image[0x4001] == ord('M')) and (image[0x4002] == ord('4'))):
    if args.verbose:
        print("This is a UM4 build")
    rp58_compatible = True
else:
    if args.verbose:
        print("Not a UM4 build, testing to see if uses an RP58 codebase")

    # If the image is not a UM4 build, we calculate the checksum of the code area $C003..$FFFF.
    # If the checksum matches an RP58 build, then this image must be RP58-compatible.
    code_m3hash=mmh3.hash(image[0x4003:], 0xFFFFFFFF, signed=False)
    if args.verbose:
        print("checksum of the code area (0x4003..0x7FFF): ", hex(code_m3hash))

    # Check against the known M3 hash of the code area in an RP58 build:
    rp58_compatible = (code_m3hash == 0x4CF503CB)

if args.verbose:
    if rp58_compatible:
        print("RP58-compatible!")
    else:
        print("NOT RP58-compatible")

if "RP58-compatible" in descriptor["eprom"]:
    if daughterboard:
        # fixme: There is no way to decode a scrambled bin file right now, so we will take the .dsc file at its word
        # about being RP58-compatible!
        fixme=True
    else:
        # The dsc file contains an entry for RP58 compatibility. We will verify that it is correct:
        stated_compatibility = descriptor["eprom"]["RP58-compatible"]
        print("stated_compatibility: ${stated_compatibility}")
        if ((stated_compatibility == "Y") and not rp58_compatible):
            fatal(f"{args.descriptorfile} states that it is RP58-compatible, but it is not")
        elif ((stated_compatibility == "N") and rp58_compatible):
            fatal(f"{args.descriptorfile} states that it is not RP58-compatible, but it is")
        elif (not((stated_compatibility == "Y") or (stated_compatibility == "N"))):
            fatal(f'{args.descriptorfile} has bad data in RP58-compatible field: must be "Y" or "N", saw "{stated_compatibility}"')
else:
    # The dsc file did not specify if it is RP58-compatible
    # We add that field now based on our calculation:
    if not rp58_compatible:
        descriptor["eprom"]["RP58-compatible"] = "N"
    else:
        descriptor["eprom"]["RP58-compatible"] = "Y"

if rp58_compatible:
    # For eproms that are RP58-compatible, we add a "mem" object within the "maps" object.
    # We don't need a maps.mem.bin object because the binary data can be extracted from the image.mem.bin object
    descriptor["eprom"]["maps"]["mem"] = {}
    descriptor["eprom"]["maps"]["mem"]["startOffset"] = mapStartOffset
    descriptor["eprom"]["maps"]["mem"]["length"] = mapLength

    # We can only calculate the checksum for unscrambled EPROMs
    if daughterboard == 'N':
        # We need to extract the monolithic map blob to calculate its checksum.
        # We don't actually store the map blob
        mapblob=image[mapStartOffset:mapStartOffset+mapLength]
        # print(f"length of mapblob: {len(mapblob)}")

        # Calculate the M3 hash of the map blob area
        mapblob_m3hash = 0xFFFFFFFF & mmh3.hash(mapblob, ~0x0, signed=False)
        if args.verbose:
            print(f"M3 hash of the mapblob: {hex(mapblob_m3hash)}")
            descriptor["eprom"]["maps"]["mem"]["m3"] = mapblob_m3hash

# Write output files as requested:

if args.csource != None:
    # Generate a BSON object from our completed DICT...
    dsc_bson = generate_bson(descriptor, args.outfile)

    # ...then use the BSON object to generate a C source code representation of itself
    generate_c_source(descriptor, dsc_bson, args.csource)

if args.json != None:
    # Generate a JSON version of the completed DICT
    generate_json(descriptor, args.json)

exit(0)
