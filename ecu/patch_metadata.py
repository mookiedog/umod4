import sys
import datetime
import subprocess
import os
import json # Added for robust JSON formatting

if len(sys.argv) < 3:
    print("Usage: patch_metadata.py <elf_file> <bin_file>")
    sys.exit(1)

ELF_FILE = sys.argv[1]
BIN_FILE = sys.argv[2]

ADDR_SYMBOL = "build_metadata_start"
SIZE_SYMBOL = "build_metadata_size"

def get_symbol_value(symbol, elf):
    try:
        output = subprocess.check_output(['m68hc11-elf-nm', '-P', elf]).decode()
        for line in output.splitlines():
            parts = line.split()
            if parts[0] == symbol:
                return int(parts[2], 16)
    except Exception as e:
        print(f"Error reading symbol {symbol}: {e}")
    return None

# 1. Extract values
address = get_symbol_value(ADDR_SYMBOL, ELF_FILE)
max_size = get_symbol_value(SIZE_SYMBOL, ELF_FILE)

if address is None or max_size is None:
    print(f"Error: Symbols not found.")
    sys.exit(1)

# 2. Calculate Offset (0xFF00 - 0x8000 = 0x7F00)
ROM_START = 0x8000
offset = address - ROM_START

# 3. Generate JSON Metadata
date_str = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
try:
    git_hash = subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD']).decode().strip()
except:
    git_hash = "unknown"

# Create a dictionary and convert to a minified JSON string (no spaces)
metadata_dict = {
    "BD": date_str,
    "GH": git_hash
}
metadata_json = json.dumps(metadata_dict, separators=(',', ':'))
metadata_bytes = metadata_json.encode('ascii')

# 4. Padding & Null Termination
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
