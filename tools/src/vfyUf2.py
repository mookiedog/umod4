#!/usr/bin/env python3
"""
UF2 File Verification Tool

Analyzes and reports on UF2 (USB Flashing Format) files.
UF2 is a file format developed by Microsoft for flashing microcontrollers
over USB mass storage.

Each UF2 block is 512 bytes with the following structure:
  Offset 0x00: Magic Start 0 (0x0A324655)
  Offset 0x04: Magic Start 1 (0x9E5D5157)
  Offset 0x08: Flags (bit 13 = family ID present)
  Offset 0x0C: Target Address (where to load in flash)
  Offset 0x10: Payload Size (typically 256 bytes)
  Offset 0x14: Block Number (sequential index)
  Offset 0x18: Total Blocks
  Offset 0x1C: Family ID (if flags bit 13 set)
  Offset 0x20: Data Payload (256 bytes)
  Offset 0x1FC: Magic End (0x0AB16F30)
"""

import sys
import struct
import argparse
from pathlib import Path
from typing import List, Tuple, Optional

# UF2 Format Constants
UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_BLOCK_SIZE = 512
UF2_PAYLOAD_SIZE = 256

# UF2 Flag Bits
UF2_FLAG_NOT_MAIN_FLASH = 0x00000001  # Bit 0
UF2_FLAG_FILE_CONTAINER = 0x00001000  # Bit 12
UF2_FLAG_FAMILY_ID_PRESENT = 0x00002000  # Bit 13
UF2_FLAG_MD5_PRESENT = 0x00004000  # Bit 14
UF2_FLAG_EXTENSION_TAGS = 0x00008000  # Bit 15

# Known Family IDs
FAMILY_IDS = {
    0x16573617: "Microchip (Atmel) ATmega32",
    0x1851780a: "Microchip (Atmel) SAML21",
    0x1b57745f: "Nordic NRF52",
    0x1c5f21b0: "ESP32",
    0x1e1f432d: "ST STM32L1xx",
    0x202e3a91: "ST STM32L0xx",
    0x21460ff0: "ST STM32WLxx",
    0x2abc77ec: "NXP LPC55xx",
    0x300f5633: "ST STM32G0xx",
    0x31d228c6: "GD32F350",
    0x04240bdf: "ST STM32L5xx",
    0x4c71240a: "ST STM32G4xx",
    0x4fb2d5bd: "NXP i.MX RT10XX",
    0x53b80f00: "ST STM32F7xx",
    0x55114460: "Microchip (Atmel) SAMD51",
    0x57755a57: "ST STM32F401",
    0x5a18069b: "Cypress FX2",
    0x5d1a0a2e: "ST STM32F2xx",
    0x5ee21072: "ST STM32F103",
    0x621e937a: "Nordic NRF52833",
    0x647824b6: "ST STM32F0xx",
    0x68ed2b88: "Microchip (Atmel) SAMD21",
    0x6b846188: "ST STM32F3xx",
    0x6d0922fa: "ST STM32F407",
    0x6db66082: "ST STM32H7xx",
    0x70d16653: "ST STM32WBxx",
    0x7eab61ed: "ESP8266",
    0x7f83e793: "NXP KL32L2x",
    0x8fb060fe: "ST STM32F407VG",
    0xada52840: "Nordic NRF52840",
    0xbfdd4eee: "ESP32-S2",
    0xc47e5767: "ESP32-S3",
    0xd42ba06c: "ESP32-C3",
    0x2b88d29c: "ESP32-C2",
    0x332726f6: "ESP32-H2",
    0x540ddf62: "ESP32-C6",
    0x3d308e94: "ESP32-P4",
    0xe48bff56: "Raspberry Pi RP2040",
    0xe48bff59: "Raspberry Pi RP2350 (ARM)",
    0xe48bff5a: "Raspberry Pi RP2350 (RISC-V)",
}


class UF2Block:
    """Represents a single UF2 block."""

    def __init__(self, data: bytes, block_index: int):
        if len(data) != UF2_BLOCK_SIZE:
            raise ValueError(f"Block {block_index}: Invalid block size {len(data)}, expected {UF2_BLOCK_SIZE}")

        # Parse header
        (magic_start0, magic_start1, self.flags, self.target_addr,
         self.payload_size, self.block_num, self.total_blocks,
         self.family_id) = struct.unpack("<8I", data[0:32])

        # Verify magic numbers
        if magic_start0 != UF2_MAGIC_START0:
            raise ValueError(f"Block {block_index}: Invalid magic start 0: 0x{magic_start0:08X}")
        if magic_start1 != UF2_MAGIC_START1:
            raise ValueError(f"Block {block_index}: Invalid magic start 1: 0x{magic_start1:08X}")

        # Extract payload
        self.payload = data[32:32 + self.payload_size]

        # Verify magic end
        magic_end = struct.unpack("<I", data[508:512])[0]
        if magic_end != UF2_MAGIC_END:
            raise ValueError(f"Block {block_index}: Invalid magic end: 0x{magic_end:08X}")

        # Store raw data for checksums if needed
        self.raw_data = data

    def has_family_id(self) -> bool:
        """Check if family ID flag is set."""
        return (self.flags & UF2_FLAG_FAMILY_ID_PRESENT) != 0

    def get_flag_names(self) -> List[str]:
        """Return human-readable flag names."""
        flag_names = []
        if self.flags & UF2_FLAG_NOT_MAIN_FLASH:
            flag_names.append("NOT_MAIN_FLASH")
        if self.flags & UF2_FLAG_FILE_CONTAINER:
            flag_names.append("FILE_CONTAINER")
        if self.flags & UF2_FLAG_FAMILY_ID_PRESENT:
            flag_names.append("FAMILY_ID_PRESENT")
        if self.flags & UF2_FLAG_MD5_PRESENT:
            flag_names.append("MD5_PRESENT")
        if self.flags & UF2_FLAG_EXTENSION_TAGS:
            flag_names.append("EXTENSION_TAGS")
        return flag_names

    def get_family_name(self) -> str:
        """Return human-readable family name."""
        if not self.has_family_id():
            return "N/A"
        return FAMILY_IDS.get(self.family_id, f"Unknown (0x{self.family_id:08X})")


class UF2File:
    """Represents a complete UF2 file."""

    def __init__(self, filepath: Path):
        self.filepath = filepath
        self.blocks: List[UF2Block] = []
        self.errors: List[str] = []

        # Read and parse all blocks
        with open(filepath, "rb") as f:
            block_index = 0
            while True:
                data = f.read(UF2_BLOCK_SIZE)
                if not data:
                    break

                if len(data) != UF2_BLOCK_SIZE:
                    self.errors.append(f"Block {block_index}: Incomplete block ({len(data)} bytes)")
                    break

                try:
                    block = UF2Block(data, block_index)
                    self.blocks.append(block)
                except ValueError as e:
                    self.errors.append(str(e))
                    break

                block_index += 1

    def validate(self) -> List[str]:
        """Validate the UF2 file structure and return warnings."""
        warnings = []

        if not self.blocks:
            warnings.append("No valid blocks found")
            return warnings

        # Check total blocks consistency
        expected_total = self.blocks[0].total_blocks
        for block in self.blocks:
            if block.total_blocks != expected_total:
                warnings.append(f"Block {block.block_num}: Inconsistent total_blocks "
                               f"({block.total_blocks} vs {expected_total})")

        # Check block numbering
        if len(self.blocks) != expected_total:
            warnings.append(f"Block count mismatch: found {len(self.blocks)} blocks, "
                           f"expected {expected_total}")

        for i, block in enumerate(self.blocks):
            if block.block_num != i:
                warnings.append(f"Block {i}: Block number mismatch "
                               f"(expected {i}, got {block.block_num})")

        # Check family ID consistency
        if self.blocks[0].has_family_id():
            expected_family = self.blocks[0].family_id
            for block in self.blocks:
                if block.has_family_id() and block.family_id != expected_family:
                    warnings.append(f"Block {block.block_num}: Inconsistent family ID "
                                   f"(0x{block.family_id:08X} vs 0x{expected_family:08X})")

        # Check for overlapping or duplicate addresses
        addr_map = {}
        for block in self.blocks:
            addr = block.target_addr
            if addr in addr_map:
                warnings.append(f"Block {block.block_num}: Duplicate target address "
                               f"0x{addr:08X} (also used by block {addr_map[addr]})")
            addr_map[addr] = block.block_num

        return warnings

    def get_address_range(self) -> Tuple[int, int, int]:
        """Return (min_addr, max_addr, total_bytes) tuple."""
        if not self.blocks:
            return (0, 0, 0)

        addresses = [(b.target_addr, b.target_addr + b.payload_size) for b in self.blocks]
        min_addr = min(start for start, _ in addresses)
        max_addr = max(end for _, end in addresses)
        total_bytes = sum(b.payload_size for b in self.blocks)

        return (min_addr, max_addr, total_bytes)

    def get_memory_regions(self) -> List[Tuple[int, int, int]]:
        """
        Return list of contiguous memory regions as (start_addr, end_addr, num_blocks).
        Regions are sorted by start address.
        """
        if not self.blocks:
            return []

        # Sort blocks by target address
        sorted_blocks = sorted(self.blocks, key=lambda b: b.target_addr)

        regions = []
        current_start = sorted_blocks[0].target_addr
        current_end = current_start + sorted_blocks[0].payload_size
        block_count = 1

        for block in sorted_blocks[1:]:
            block_start = block.target_addr
            block_end = block_start + block.payload_size

            # Check if this block is contiguous with current region
            if block_start == current_end:
                current_end = block_end
                block_count += 1
            else:
                # Save current region and start new one
                regions.append((current_start, current_end, block_count))
                current_start = block_start
                current_end = block_end
                block_count = 1

        # Save final region
        regions.append((current_start, current_end, block_count))

        return regions

    def print_report(self, verbose: bool = False):
        """Print a detailed report about the UF2 file."""
        print(f"UF2 File Analysis: {self.filepath.name}")
        print("=" * 70)

        # File info
        file_size = self.filepath.stat().st_size
        print(f"File size: {file_size:,} bytes ({file_size / 1024:.1f} KB)")
        print(f"Blocks found: {len(self.blocks)}")

        if self.errors:
            print("\nERRORS:")
            for error in self.errors:
                print(f"  - {error}")
            return

        if not self.blocks:
            print("\nNo valid blocks found in file")
            return

        # Basic block info
        first_block = self.blocks[0]
        print(f"Total blocks declared: {first_block.total_blocks}")
        print(f"Payload size per block: {first_block.payload_size} bytes")

        # Flags
        print(f"\nFlags: 0x{first_block.flags:08X}")
        flag_names = first_block.get_flag_names()
        if flag_names:
            for flag_name in flag_names:
                print(f"  - {flag_name}")
        else:
            print("  (no flags set)")

        # Family ID
        if first_block.has_family_id():
            print(f"\nFamily ID: 0x{first_block.family_id:08X}")
            print(f"  {first_block.get_family_name()}")
        else:
            print("\nFamily ID: Not present")

        # Address range
        min_addr, max_addr, total_bytes = self.get_address_range()
        print(f"\nAddress Range:")
        print(f"  Start: 0x{min_addr:08X}")
        print(f"  End:   0x{max_addr:08X}")
        print(f"  Span:  {max_addr - min_addr:,} bytes ({(max_addr - min_addr) / 1024:.1f} KB)")
        print(f"  Total payload: {total_bytes:,} bytes ({total_bytes / 1024:.1f} KB)")

        # Memory regions
        regions = self.get_memory_regions()
        if len(regions) > 1:
            print(f"\nMemory Regions: {len(regions)} non-contiguous regions")
            for i, (start, end, count) in enumerate(regions):
                size = end - start
                print(f"  Region {i + 1}: 0x{start:08X}-0x{end:08X} "
                      f"({size:,} bytes, {count} blocks)")
                if i < len(regions) - 1:
                    gap = regions[i + 1][0] - end
                    if gap > 0:
                        print(f"    Gap: {gap:,} bytes (0x{gap:X})")
        else:
            print(f"\nMemory Regions: 1 contiguous region")
            start, end, count = regions[0]
            print(f"  0x{start:08X}-0x{end:08X} ({end - start:,} bytes, {count} blocks)")

        # Validation warnings
        warnings = self.validate()
        if warnings:
            print("\nWARNINGS:")
            for warning in warnings:
                print(f"  - {warning}")
        else:
            print("\nValidation: OK")

        # Verbose block-by-block details
        if verbose and self.blocks:
            print("\n" + "=" * 70)
            print("VERBOSE BLOCK DETAILS:")
            print("=" * 70)

            prev_block = None
            for i, block in enumerate(self.blocks):
                # Calculate address range
                start_addr = block.target_addr
                end_addr = start_addr + block.payload_size - 1

                # Build the output line
                line = f"Block {block.block_num:4d}: 0x{start_addr:08X}..0x{end_addr:08X}"

                # Check if non-contiguous (skip for first block)
                if i > 0:
                    expected_addr = prev_block.target_addr + prev_block.payload_size
                    if block.target_addr != expected_addr:
                        gap = block.target_addr - expected_addr
                        if gap > 0:
                            line += f"  Non-contiguous! (gap: {gap} bytes, 0x{gap:X})"
                        else:
                            line += f"  Non-contiguous! (overlap: {-gap} bytes)"

                # Check for changed flags or family ID
                is_first = (i == 0)
                flags_changed = is_first or (block.flags != prev_block.flags)
                family_changed = is_first or (block.family_id != prev_block.family_id)

                if flags_changed:
                    flag_names = block.get_flag_names()
                    if flag_names:
                        line += f"  Flags: 0x{block.flags:08X} ({', '.join(flag_names)})"
                    else:
                        line += f"  Flags: 0x{block.flags:08X}"

                if family_changed:
                    if block.has_family_id():
                        line += f"  Family: 0x{block.family_id:08X} ({block.get_family_name()})"
                    else:
                        line += f"  Family: Not present"

                print(line)
                prev_block = block


def main():
    parser = argparse.ArgumentParser(
        description="Analyze and verify UF2 (USB Flashing Format) files",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  vfyUf2.py firmware.uf2
  vfyUf2.py build/EP/EP.uf2
  vfyUf2.py -v build/WP/WP.uf2
        """
    )
    parser.add_argument("uf2_file", type=Path, help="Path to UF2 file to analyze")
    parser.add_argument("-v", "--verbose", action="store_true",
                       help="Print detailed information for each block")

    args = parser.parse_args()

    # Check file exists
    if not args.uf2_file.exists():
        print(f"Error: File not found: {args.uf2_file}", file=sys.stderr)
        return 1

    if not args.uf2_file.is_file():
        print(f"Error: Not a file: {args.uf2_file}", file=sys.stderr)
        return 1

    # Parse and analyze
    try:
        uf2 = UF2File(args.uf2_file)
        uf2.print_report(verbose=args.verbose)

        # Return non-zero if there were errors
        if uf2.errors:
            return 1

    except Exception as e:
        print(f"Error analyzing file: {e}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
