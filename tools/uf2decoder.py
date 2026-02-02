import struct
import argparse
import sys

def decode_uf2_ranges(filename):
    UF2_MAGIC_START0 = 0x0A324655
    UF2_MAGIC_START1 = 0x9E5D5157
    UF2_MAGIC_END   = 0x0AB16F30

    raw_blocks = []

    try:
        with open(filename, "rb") as f:
            while True:
                block = f.read(512)
                if len(block) < 512:
                    break

                # Unpack header: magic0, magic1, flags, addr, payload_size, block_no, total_blocks, family_id
                header = struct.unpack("<IIIIIIII", block[:32])
                magic0, magic1, flags, addr, num_bytes, block_no, total_blocks, family_id = header

                footer = struct.unpack("<I", block[508:512])[0]

                if magic0 == UF2_MAGIC_START0 and magic1 == UF2_MAGIC_START1 and footer == UF2_MAGIC_END:
                    if num_bytes > 0:
                        raw_blocks.append((addr, addr + num_bytes))

        if not raw_blocks:
            return []

        # Sort blocks by start address to make merging easier
        raw_blocks.sort()

        merged_ranges = []
        curr_start, curr_end = raw_blocks[0]

        for next_start, next_end in raw_blocks[1:]:
            # If the next block starts exactly where the current one ends, bridge them
            if next_start == curr_end:
                curr_end = next_end
            # If there's a gap, save the current range and start a new one
            elif next_start > curr_end:
                merged_ranges.append((curr_start, curr_end))
                curr_start, curr_end = next_start, next_end
            # Handle overlapping blocks if they exist
            else:
                curr_end = max(curr_end, next_end)

        merged_ranges.append((curr_start, curr_end))
        return merged_ranges

    except FileNotFoundError:
        print(f"Error: The file '{filename}' was not found.")
        sys.exit(1)
    except Exception as e:
        print(f"An error occurred: {e}")
        sys.exit(1)

def main():
    parser = argparse.ArgumentParser(description="Decode UF2 file memory ranges.")
    parser.add_argument("filename", help="Path to the .uf2 file")

    args = parser.parse_args()

    ranges = decode_uf2_ranges(args.filename)

    if not ranges:
        print("No valid UF2 data blocks found.")
        return

    print(f"\n{'START ADDR':<12} | {'END ADDR':<12} | {'SIZE':<10}")
    print("-" * 40)
    for start, end in ranges:
        size_kb = (end - start) / 1024
        print(f"0x{start:08X}   | 0x{end:08X}   | {size_kb:>6.2f} KB")
    print("")

if __name__ == "__main__":
    main()
