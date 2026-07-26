#!/usr/bin/env python3
"""Build a tiny arch-tagged ELF64 fixture for host smoke tests."""

from __future__ import annotations

import argparse
import pathlib
import struct


MACHINES = {
    "x86_64": 62,
    "aarch64": 183,
    "riscv64": 243,
}


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def build_x86_64_serial_code(message: bytes) -> bytes:
    code = bytearray()
    for byte in message:
        code += b"\x66\xba\xfd\x03"  # mov dx, 0x3fd
        code += b"\xec"              # in al, dx
        code += b"\xa8\x20"          # test al, 0x20
        code += b"\x74\xf7"          # jz -9
        code += b"\x66\xba\xf8\x03"  # mov dx, 0x3f8
        code += b"\xb0" + bytes([byte])
        code += b"\xee"              # out dx, al
    code += b"\xfa\xf4\xeb\xfd"      # cli; hlt; jmp -3
    return bytes(code)

def build_x86_64_entry_code(expected_entry_flags: int) -> bytes:
    success = build_x86_64_serial_code(b"PARUS-FIXTURE-ENTRY-OK\r\n")
    failure = build_x86_64_serial_code(b"PARUS-FIXTURE-ENTRY-ABI-FAIL\r\n")
    code = bytearray()
    code += b"\x48\x85\xff"  # test rdi, rdi
    first_jump = len(code)
    code += b"\x0f\x84\x00\x00\x00\x00"  # jz failure
    code += b"\x48\x81\xfe" + struct.pack("<I", expected_entry_flags)  # cmp rsi, imm32
    second_jump = len(code)
    code += b"\x0f\x85\x00\x00\x00\x00"  # jne failure
    code += success
    failure_offset = len(code)
    code += failure
    struct.pack_into("<i", code, first_jump + 2, failure_offset - (first_jump + 6))
    struct.pack_into("<i", code, second_jump + 2, failure_offset - (second_jump + 6))
    return bytes(code)


def pack_u32(value: int) -> bytes:
    return struct.pack("<I", value & 0xFFFFFFFF)


def encode_aarch64_mov_wide(base: int, register: int, immediate: int, shift: int) -> int:
    if shift % 16 != 0 or shift < 0 or shift > 48:
        raise ValueError("AArch64 wide move shift must be 0, 16, 32, or 48")
    if immediate < 0 or immediate > 0xFFFF:
        raise ValueError("AArch64 wide move immediate does not fit in 16 bits")
    return base | ((shift // 16) << 21) | (immediate << 5) | register


def encode_aarch64_adr(register: int, offset: int) -> int:
    if offset < -(1 << 20) or offset >= (1 << 20):
        raise ValueError("AArch64 ADR offset is out of range")
    encoded = offset & ((1 << 21) - 1)
    return 0x10000000 | ((encoded & 0x3) << 29) | (((encoded >> 2) & 0x7FFFF) << 5) | register


def encode_aarch64_branch(base: int, bits: int, instruction_offset: int, target_offset: int) -> int:
    branch_offset = target_offset - instruction_offset
    if branch_offset % 4 != 0:
        raise ValueError("AArch64 branch target is not instruction aligned")
    immediate = branch_offset // 4
    minimum = -(1 << (bits - 1))
    maximum = 1 << (bits - 1)
    if immediate < minimum or immediate >= maximum:
        raise ValueError("AArch64 branch offset is out of range")
    return base | (immediate & ((1 << bits) - 1))


def encode_aarch64_imm19(base: int, instruction_offset: int, target_offset: int) -> int:
    branch_offset = target_offset - instruction_offset
    if branch_offset % 4 != 0:
        raise ValueError("AArch64 imm19 branch target is not instruction aligned")
    immediate = branch_offset // 4
    if immediate < -(1 << 18) or immediate >= (1 << 18):
        raise ValueError("AArch64 imm19 branch offset is out of range")
    return base | ((immediate & 0x7FFFF) << 5)


def encode_aarch64_cbz_w(register: int, instruction_offset: int, target_offset: int) -> int:
    return encode_aarch64_imm19(0x34000000 | register, instruction_offset, target_offset)

def encode_aarch64_cbz_x(register: int, instruction_offset: int, target_offset: int) -> int:
    return encode_aarch64_imm19(0xB4000000 | register, instruction_offset, target_offset)


def encode_aarch64_b_ne(instruction_offset: int, target_offset: int) -> int:
    return encode_aarch64_imm19(0x54000001, instruction_offset, target_offset)


def encode_aarch64_tbz_w(
    register: int,
    bit: int,
    instruction_offset: int,
    target_offset: int,
) -> int:
    if bit < 0 or bit > 31:
        raise ValueError("AArch64 32-bit TBZ bit must be in range 0..31")
    branch_offset = target_offset - instruction_offset
    if branch_offset % 4 != 0:
        raise ValueError("AArch64 TBZ target is not instruction aligned")
    immediate = branch_offset // 4
    if immediate < -(1 << 13) or immediate >= (1 << 13):
        raise ValueError("AArch64 TBZ offset is out of range")
    return 0x36000000 | (bit << 19) | ((immediate & 0x3FFF) << 5) | register


def build_aarch64_serial_code(message: bytes) -> bytes:
    # QEMU virt exposes the first PL011 UART at physical address 0x09000000.
    loop = 3 * 4
    wait = 6 * 4
    write = 9 * 4
    done = 11 * 4
    message_offset = 13 * 4
    words = [
        encode_aarch64_mov_wide(0xD2800000, 0, 0x0000, 0),              # movz x0, #0
        encode_aarch64_mov_wide(0xF2800000, 0, 0x0900, 16),             # movk x0, #0x0900, lsl #16
        encode_aarch64_adr(1, message_offset - (2 * 4)),               # adr x1, message
        0x39400022,                                                     # ldrb w2, [x1]
        0x91000421,                                                     # add x1, x1, #1
        encode_aarch64_cbz_w(2, 5 * 4, done),                           # cbz w2, done
        0xB9401803,                                                     # ldr w3, [x0, #0x18]
        encode_aarch64_tbz_w(3, 5, 7 * 4, write),                       # tbz w3, #5, write
        encode_aarch64_branch(0x14000000, 26, 8 * 4, wait),             # b wait
        0xB9000002,                                                     # str w2, [x0]
        encode_aarch64_branch(0x14000000, 26, 10 * 4, loop),            # b loop
        0xD503205F,                                                     # wfe
        encode_aarch64_branch(0x14000000, 26, 12 * 4, done),            # b done
    ]
    code = bytearray()
    for word in words:
        code += pack_u32(word)
    code += message + b"\x00"
    return bytes(code)

def build_aarch64_entry_code(expected_entry_flags: int) -> bytes:
    if expected_entry_flags < 0 or expected_entry_flags > 0xFFF:
        raise ValueError("AArch64 expected entry flags do not fit CMP immediate")
    success = build_aarch64_serial_code(b"PARUS-FIXTURE-ENTRY-OK\r\n")
    failure = build_aarch64_serial_code(b"PARUS-FIXTURE-ENTRY-ABI-FAIL\r\n")
    success_padding = b"\x00" * (align_up(len(success), 4) - len(success))
    failure_offset = 12 + len(success) + len(success_padding)
    words = [
        encode_aarch64_cbz_x(0, 0, failure_offset),
        0xF100001F | (expected_entry_flags << 10) | (1 << 5),  # cmp x1, #flags
        encode_aarch64_b_ne(8, failure_offset),
    ]
    code = bytearray()
    for word in words:
        code += pack_u32(word)
    code += success
    code += success_padding
    code += failure
    return bytes(code)


def build_fixture(
    machine: int,
    base: int,
    layout: str,
    high_base: int,
    entry_at_base: bool,
    expected_entry_flags: int,
) -> bytes:
    entry_offset = 0 if entry_at_base else 0x78
    segment_offset = 0x1000 if entry_at_base else 0
    if layout == "higher-half":
        virtual_base = high_base
        physical_base = base
        marker = b"RIBONHHK"
    else:
        virtual_base = base
        physical_base = base
        marker = b"RIBONKRN"
    entry = virtual_base + entry_offset

    if machine == MACHINES["x86_64"]:
        entry_code = build_x86_64_entry_code(expected_entry_flags)
    elif machine == MACHINES["aarch64"]:
        entry_code = build_aarch64_entry_code(expected_entry_flags)
    else:
        entry_code = marker
    segment_alignment = 0x1000 if entry_at_base else 0x200000
    image = bytearray(max(0x90, segment_offset + align_up(entry_offset + len(entry_code), 16)))

    image[0:16] = bytes(
        [
            0x7F,
            ord("E"),
            ord("L"),
            ord("F"),
            2,
            1,
            1,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
        ]
    )
    struct.pack_into("<HHIQQQIHHHHHH", image, 16, 2, machine, 1, entry, 64, 0, 0, 64, 56, 1, 0, 0, 0)
    struct.pack_into(
        "<IIQQQQQQ",
        image,
        64,
        1,
        5,
        segment_offset,
        virtual_base,
        physical_base,
        len(image) - segment_offset,
        0x1000,
        segment_alignment,
    )
    image[segment_offset + entry_offset : segment_offset + entry_offset + len(entry_code)] = entry_code
    return bytes(image)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--arch", choices=sorted(MACHINES), required=True)
    parser.add_argument("--base", type=lambda value: int(value, 0), default=0x200000)
    parser.add_argument("--layout", choices=("low", "higher-half"), default="low")
    parser.add_argument("--high-base", type=lambda value: int(value, 0), default=0xFFFFFFFF80000000)
    parser.add_argument("--entry-at-base", action="store_true")
    parser.add_argument("--expected-entry-flags", type=lambda value: int(value, 0), default=0x1)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(
        build_fixture(
            MACHINES[args.arch],
            args.base,
            args.layout,
            args.high_base,
            args.entry_at_base,
            args.expected_entry_flags,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
