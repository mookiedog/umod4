"""
Read symbol values from an ARM ELF using arm-none-eabi-nm.

Usage:
    syms = read_symbols("build/EP/EP.elf", ["__unused_flash_start__", "__unused_flash_size__"])
    start = syms["__unused_flash_start__"]
    size  = syms["__unused_flash_size__"]
"""

import subprocess
import os

# Every build directory has an arm_toolchain_latest symlink to the bin directory of the ARM
# toolchain that built it (see cmake/toolchains/arm-none-eabi.cmake).  nm comes from the link
# next to the ELF, so it always matches the compiler that produced that ELF.  The top-level
# build/arm_toolchain_latest is not used: it only exists if the top-level project was configured
# with the ARM toolchain file, which VS Code does and a plain terminal cmake command does not.
def _nm_for(elf_path):
    return os.path.join(os.path.dirname(os.path.abspath(elf_path)), "arm_toolchain_latest",
                        "arm-none-eabi-nm")


class ElfError(Exception):
    pass


def read_symbols(elf_path, names):
    """
    Return a dict mapping each name in `names` to its integer value.
    Raises ElfError if nm fails or any symbol is not found.
    """
    if not os.path.exists(elf_path):
        raise ElfError(f"ELF not found: {elf_path}")
    nm = _nm_for(elf_path)
    if not os.path.exists(nm):
        raise ElfError(f"nm not found: {nm}")

    result = subprocess.run(
        [nm, "--defined-only", elf_path],
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
