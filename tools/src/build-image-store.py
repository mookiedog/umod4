# Build image store artifacts for the EP's image_store flash partition.
#
# Each 64K slot holds a self-describing EPROM image:
#   - First 32768 bytes: BSON header {name, description, image_m3, protection (optional)}
#     zero-padded to exactly 32768 bytes
#   - Next 32768 bytes: raw 32KB EPROM binary
#
# Slot 0 is always the image selector: a BSON document describing how the EP
# constructs its boot image from named slots.  Image composition (code + mapblob
# splicing) happens at EP boot time, NOT at build time — each slot holds a single
# complete 32KB image.
#
# Usage:
#   build-image-store.py -c config.json -o output_dir --search-path dir1 --search-path dir2
#
# Config format:
#   {
#       "images": [
#           {"name": "UM4",  "description": "UM4 datalogging firmware", "bin": "UM4.bin"},
#           {"name": "RP58", "description": "RP58 stock EPROM",         "bin": "RP58.bin",
#            "protection": "A"}
#       ],
#       "selector": {
#           "images": [
#               {"code": "UM4", "mapblob": "RP58"},
#               {"code": "UM4"}
#           ]
#       }
#   }
#
# Output files (written to output_dir/):
#   <name>.slot       One 64K slot file per image
#   slot0.slot        Slot 0: selector BSON padded to 64K
#   image_store.bin   Compact binary: slot 0 + all image slots in order (no 8MB padding)

import sys
import os
import json
import argparse
import mmh3
import struct

from bson import BSON

IMAGE_SIZE   = 32768   # 32KB: the raw EPROM image
HEADER_SIZE  = 32768   # 32KB: the BSON header region (padded)
SLOT_SIZE    = IMAGE_SIZE + HEADER_SIZE  # 64K per slot


def fatal(msg):
    print(f"FATAL: {msg}", file=sys.stderr)
    sys.exit(1)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Build image store artifacts for the EP's image_store flash partition."
    )
    parser.add_argument("-c", "--config", required=True,
                        help="Input JSON config file")
    parser.add_argument("-o", "--output-dir", required=True,
                        help="Output directory for .slot files and image_store.bin")
    parser.add_argument("--search-path", action="append", default=[],
                        dest="search_paths",
                        help="Directory to search for .bin files (can repeat)")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Verbose output")
    parser.add_argument("--output-c", metavar="FILE",
                        help="Also write a C source file with image store data in the "
                             ".image_store linker section (for linking into EP firmware)")
    return parser.parse_args()


def find_bin(name, search_paths):
    for d in search_paths:
        path = os.path.join(d, name)
        if os.path.isfile(path):
            return path
    fatal(f"Cannot find \"{name}\" in search paths: {search_paths}")


def read_bin(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) != IMAGE_SIZE:
        fatal(f"{path}: expected {IMAGE_SIZE} bytes, got {len(data)}")
    return data


def build_slot(name, description, image_bytes, protection=None):
    """Build a 64K slot: 32KB BSON header (zero-padded) + 32KB image."""
    # Compute murmur3 hash of the image (seed 0xFFFFFFFF, keep lower 32 bits)
    image_m3 = 0xFFFFFFFF & mmh3.hash(image_bytes, 0xFFFFFFFF, signed=False)

    # Build BSON header document
    doc = {
        "name": name,
        "description": description,
        "image_m3": image_m3,
    }
    if protection and protection != "N":
        doc["protection"] = protection

    bson_bytes = BSON.encode(doc)
    if len(bson_bytes) > HEADER_SIZE:
        fatal(f"Slot \"{name}\": BSON header is {len(bson_bytes)} bytes, exceeds {HEADER_SIZE}")

    # Zero-pad BSON header to exactly HEADER_SIZE bytes
    header = bytearray(HEADER_SIZE)
    header[:len(bson_bytes)] = bson_bytes

    slot = bytes(header) + image_bytes
    assert len(slot) == SLOT_SIZE
    return slot, image_m3


def build_selector_slot(selector_config):
    """Build slot 0: the image selector BSON document, zero-padded to 64K."""
    # The selector BSON schema: {"images": [{"code": "...", "mapblob": "..." (opt)}, ...]}
    doc = {"images": selector_config["images"]}
    bson_bytes = BSON.encode(doc)
    if len(bson_bytes) > SLOT_SIZE:
        fatal(f"Selector BSON is {len(bson_bytes)} bytes, exceeds slot size {SLOT_SIZE}")

    slot = bytearray(SLOT_SIZE)
    slot[:len(bson_bytes)] = bson_bytes
    return bytes(slot)


###################################################################################################
# Main

args = parse_args()

# Read config
try:
    with open(args.config, "r") as f:
        config = json.load(f)
except Exception as e:
    fatal(f"Unable to read config file <{args.config}>: {e}")

if "images" not in config or not isinstance(config["images"], list):
    fatal("Config must contain an \"images\" list")
if len(config["images"]) == 0:
    fatal("Config \"images\" list must not be empty")
if "selector" not in config:
    fatal("Config must contain a \"selector\" section")

os.makedirs(args.output_dir, exist_ok=True)

# Build each image slot (slots 1..N)
slots = []  # list of (name, slot_bytes)
names_seen = set()

for entry in config["images"]:
    name = entry.get("name")
    if not name:
        fatal("Each image entry must have a \"name\" key")
    if name in names_seen:
        fatal(f"Duplicate image name: \"{name}\"")
    names_seen.add(name)

    description = entry.get("description", "")
    protection  = entry.get("protection", "N")

    if "bin" not in entry:
        fatal(f"Image \"{name}\": must have a \"bin\" key")

    bin_path = find_bin(entry["bin"], args.search_paths)
    image_bytes = read_bin(bin_path)

    slot_bytes, image_m3 = build_slot(name, description, image_bytes, protection)
    slots.append((name, slot_bytes))

    # Write individual .slot file
    slot_file = os.path.join(args.output_dir, f"{name}.slot")
    with open(slot_file, "wb") as f:
        f.write(slot_bytes)

    if args.verbose:
        print(f"  Slot {len(slots)}: \"{name}\" m3=0x{image_m3:08X} protection={protection!r}"
              f"  -> {slot_file}")

# Build slot 0 (selector)
selector_slot = build_selector_slot(config["selector"])
slot0_file = os.path.join(args.output_dir, "slot0.slot")
with open(slot0_file, "wb") as f:
    f.write(selector_slot)
if args.verbose:
    print(f"  Slot 0: selector ({len(config['selector']['images'])} entries) -> {slot0_file}")

# Build compact image_store.bin: slot 0 + slot 1 + ... + slot N
# (only the populated slots, no 8MB padding)
image_store = bytearray()
image_store += selector_slot          # slot 0
for name, slot_bytes in slots:
    image_store += slot_bytes         # slots 1..N

image_store_file = os.path.join(args.output_dir, "image_store.bin")
with open(image_store_file, "wb") as f:
    f.write(image_store)

if args.verbose:
    total_slots = 1 + len(slots)
    print(f"image_store.bin: {total_slots} slots ({len(image_store)} bytes) -> {image_store_file}")

if args.output_c:
    with open(args.output_c, "w") as f:
        f.write("/* Auto-generated by build-image-store.py — do not edit */\n")
        f.write("__attribute__((section(\".image_store\")))\n")
        f.write("const unsigned char image_store_data[] = {\n")
        for i, b in enumerate(image_store):
            if i % 16 == 0:
                f.write("    ")
            f.write(f"0x{b:02x},")
            if i % 16 == 15:
                f.write("\n")
            else:
                f.write(" ")
        if len(image_store) % 16 != 0:
            f.write("\n")
        f.write("};\n")
    if args.verbose:
        print(f"image_store_data.c: {len(image_store)} bytes -> {args.output_c}")

if args.verbose:
    print("Done.")
