"""
Read symbol values from an ARM ELF using arm-none-eabi-nm.

Usage:
    syms = read_symbols("build/EP/EP.elf", ["__unused_flash_start__", "__unused_flash_size__"])
    start = syms["__unused_flash_start__"]
    size  = syms["__unused_flash_size__"]
"""

import subprocess
import os

NM = "/opt/arm/arm-none-eabi/15.2.rel1/bin/arm-none-eabi-nm"


class ElfError(Exception):
    pass


def read_symbols(elf_path, names):
    """
    Return a dict mapping each name in `names` to its integer value.
    Raises ElfError if nm fails or any symbol is not found.
    """
    if not os.path.exists(elf_path):
        raise ElfError(f"ELF not found: {elf_path}")
    if not os.path.exists(NM):
        raise ElfError(f"nm not found: {NM}")

    result = subprocess.run(
        [NM, "--defined-only", elf_path],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        raise ElfError(f"nm failed: {result.stderr.strip()}")

    found = {}
    for line in result.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            sym_name = parts[2]
            if sym_name in names:
                found[sym_name] = int(parts[0], 16)

    missing = set(names) - set(found)
    if missing:
        raise ElfError(f"Symbols not found in {elf_path}: {sorted(missing)}")

    return found
