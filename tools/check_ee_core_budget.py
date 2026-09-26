#!/usr/bin/env python3
# Original LUNA code: Danny Nunez (dnunezx) 2026
"""Fail when Neutrino's EE core approaches LUNA's protected stack region."""

import argparse
import struct
from pathlib import Path


def integer(value: str) -> int:
    return int(value, 0)


def load_end(path: Path) -> int:
    data = path.read_bytes()
    if len(data) < 52 or data[:4] != b"\x7fELF" or data[4] != 1:
        raise ValueError("expected a 32-bit ELF file")
    byte_order = "<" if data[5] == 1 else ">" if data[5] == 2 else None
    if byte_order is None:
        raise ValueError("unsupported ELF byte order")
    program_offset = struct.unpack_from(byte_order + "I", data, 28)[0]
    entry_size, entry_count = struct.unpack_from(byte_order + "HH", data, 42)
    if entry_size < 32 or program_offset + entry_size * entry_count > len(data):
        raise ValueError("invalid ELF program-header table")

    result = 0
    for index in range(entry_count):
        offset = program_offset + index * entry_size
        segment_type, _, virtual_address, _, _, memory_size = struct.unpack_from(
            byte_order + "IIIIII", data, offset
        )
        if segment_type == 1:  # PT_LOAD
            result = max(result, virtual_address + memory_size)
    if result == 0:
        raise ValueError("ELF has no loadable segment")
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("elf", type=Path)
    parser.add_argument("--stack-start", type=integer, default=0x00094000)
    parser.add_argument("--minimum-headroom", type=integer, default=0x00004000)
    args = parser.parse_args()

    end = load_end(args.elf)
    headroom = args.stack_start - end
    print(
        f"EE core end=0x{end:08x}, stack=0x{args.stack_start:08x}, "
        f"headroom=0x{headroom:08x} ({headroom:,} bytes)"
    )
    if headroom < args.minimum_headroom:
        print(
            f"ERROR: headroom is below the 0x{args.minimum_headroom:08x} "
            "release budget"
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
