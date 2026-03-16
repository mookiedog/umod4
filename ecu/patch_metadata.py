import sys
import json
import subprocess

# Argument order:
# sys.argv[0] = script name
# sys.argv[1] = JSON_FILE  (system_metadata.json from build dir)
# sys.argv[2] = ELF_FILE   (UM4.elf)
# sys.argv[3] = BIN_FILE   (UM4.bin, patched in-place)

JSON_FILE = sys.argv[1]
ELF_FILE  = sys.argv[2]
BIN_FILE  = sys.argv[3]

def get_symbol_value(symbol, elf):
    try:
        output = subprocess.check_output(['m68hc11-elf-nm', '-P', elf]).decode()
        for line in output.splitlines():
            parts = line.split()
            if parts[0] == symbol:
                return int(parts[2], 16)
    except Exception as e:
        print(f"Error reading symbol {symbol} from {elf}: {e}")
    return None

# Get addresses from the ELF
address  = get_symbol_value("build_metadata_start", ELF_FILE)
max_size = get_symbol_value("build_metadata_size",  ELF_FILE)

if address is None or max_size is None:
    print(f"Error: Symbols 'build_metadata_start' or 'build_metadata_size' not found in {ELF_FILE}.")
    sys.exit(1)

# Load the metadata
with open(JSON_FILE, 'r') as f:
    meta_data = json.load(f)

# Format as a compact JSON string
metadata_str   = json.dumps(meta_data, separators=(',', ':'))
metadata_bytes = metadata_str.encode('ascii')

if len(metadata_bytes) >= max_size:
    print(f"Warning: metadata string ({len(metadata_bytes)} bytes) exceeds reserved space ({max_size} bytes).")
    sys.exit(2)

# Pad with nulls to fill the reserved block (guarantees null termination)
block = metadata_bytes.ljust(max_size, b'\x00')

# Calculate the byte offset within the .bin file.
# The HC11 EPROM is linked to start at ROM_START; the .bin file starts at byte 0
# corresponding to ROM_START, so offset = address - ROM_START.
ROM_START = 0x8000
offset = address - ROM_START

try:
    with open(BIN_FILE, "r+b") as f:
        f.seek(offset)
        f.write(block)
    print(f"Patched metadata into {BIN_FILE} at offset {hex(offset)}: {metadata_str}")
except Exception as e:
    print(f"Patching failed: {e}")
    sys.exit(1)
