import sys
import json
import subprocess

# New argument order:
# sys.argv[0] = script name
# sys.argv[1] = JSON_FILE
# sys.argv[2] = ELF_FILE
# sys.argv[3] = BIN_FILE

JSON_FILE = sys.argv[1]
ELF_FILE  = sys.argv[2]
BIN_FILE  = sys.argv[3]

def get_symbol_value(symbol, elf):
    try:
        # CRITICAL: Use ELF_FILE here, not the JSON file!
        output = subprocess.check_output(['m68hc11-elf-nm', '-P', elf]).decode()
        for line in output.splitlines():
            parts = line.split()
            if parts[0] == symbol:
                return int(parts[2], 16)
    except Exception as e:
        print(f"Error reading symbol {symbol} from {elf}: {e}")
    return None

# Load the metadata
with open(JSON_FILE, 'r') as f:
    meta_data = json.load(f)

# Format the string
metadata_json = json.dumps(meta_data, separators=(',', ':'))
metadata_bytes = metadata_json.encode('ascii')

# Get addresses from the ELF
address = get_symbol_value("build_metadata_start", ELF_FILE)
max_size = get_symbol_value("build_metadata_size", ELF_FILE)

# ... proceed with the patching logic ...

if address is None or max_size is None:
    print(f"Error: Symbols not found.")
    sys.exit(1)

# Calculate Offset (0xFF00 - 0x8000 = 0x7F00)
ROM_START = 0x8000
offset = address - ROM_START

# Read metadata from the JSON file passed from Superbuild
with open(sys.argv[1], 'r') as f:
    data = json.load(f)

# Format as a string for the EPROM
metadata_str = json.dumps(data, separators=(',', ':'))
block = metadata_str.encode('ascii').ljust(max_size, b'\x00')

# Padding & Null Termination
# We truncate at max_size - 1 to guarantee at least one null byte at the end
if len(metadata_bytes) >= max_size:
    print(f"Warning: JSON too long.")
    sys.exit(2)

# Pad with nulls. This ensures the block is exactly max_size and ends with \0
block = metadata_bytes.ljust(max_size, b'\x00')

# 5. Patch
try:
    with open(BIN_FILE, "r+b") as f:
        f.seek(offset)
        f.write(block)
    print(f"Patched JSON metadata: {metadata_json}")
    print(f"At {hex(address)} (Offset {hex(offset)})")
except Exception as e:
    print(f"Patching failed: {e}")
    sys.exit(1)
