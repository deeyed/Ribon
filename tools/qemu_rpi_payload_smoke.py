#!/usr/bin/env python3
"""Run the Ribon Raspberry Pi native payload under QEMU AArch64 virt."""

from __future__ import annotations

import argparse
import os
import pathlib
import select
import subprocess
import sys
import time


MARKER = "RIBON-RPI-HANDOFF-SMOKE-OK"
REQUIRED_MARKERS = (
    "RIBON-RPI-HANDOFF-START",
    "RIBON-RPI-DTB-SIZE=0x",
    "RIBON-RPI-DTB-SOURCE=firmware",
    "RIBON-RPI-DTB-RPH1=descriptor",
    "RIBON-RPI-CMDLINE-SOURCE=",
    "RIBON-RPI-BOOT-MEDIA=package",
    "RIBON-RPI-BOOT-MEDIA-BACKEND=embedded-package",
    "RIBON-RPI-BOOT-KERNEL-PATH=kernel/kernel.elf",
    "RIBON-RPI-BOOT-CMDLINE-PATH=cmdline.txt",
    "RIBON-RPI-BOOT-CONFIG-PATH=config.txt",
    "RIBON-RPI-LOADER-STATUS=0x",
    "RIBON-RPI-SEGMENT-COUNT=0x",
    "RIBON-RPI-KERNEL-ENTRY=0x",
    "RIBON-RPI-SEGMENT-COPY-OK",
    "RIBON-RPI-RPH1=0x",
    "RIBON-RPI-RPH1-SIZE=0x",
    "RIBON-RPI-RPH1-SECTIONS=0x",
    "RIBON-RPI-HANDOFF-X0=0x",
    "RIBON-RPI-HANDOFF-X1=0x",
    "RIBON-RPI-HANDOFF-READY",
    MARKER,
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--image", required=True, type=pathlib.Path)
    parser.add_argument("--log", required=True, type=pathlib.Path)
    parser.add_argument("--timeout", type=float, default=15.0)
    args = parser.parse_args()

    if not args.image.exists():
        print(f"RIBON-RPI-SMOKE-FAIL: missing image: {args.image}", file=sys.stderr)
        return 1

    command = [
        args.qemu,
        "-machine",
        "virt",
        "-cpu",
        "cortex-a76",
        "-m",
        "256M",
        "-nographic",
        "-monitor",
        "none",
        "-serial",
        "stdio",
        "-kernel",
        str(args.image),
    ]

    output_parts: list[bytes] = []
    timed_out = False
    return_code: int | None = None
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    assert process.stdout is not None
    stdout_fd = process.stdout.fileno()
    os.set_blocking(stdout_fd, False)
    deadline = time.monotonic() + args.timeout
    try:
        while True:
            output = b"".join(output_parts).decode("utf-8", "replace")
            if MARKER in output:
                process.terminate()
                try:
                    process.wait(timeout=2.0)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                break
            return_code = process.poll()
            if return_code is not None:
                try:
                    remainder = os.read(stdout_fd, 65536)
                    if remainder:
                        output_parts.append(remainder)
                except BlockingIOError:
                    pass
                break
            remaining = deadline - time.monotonic()
            if remaining <= 0.0:
                timed_out = True
                process.kill()
                remainder = process.communicate()[0]
                if remainder:
                    output_parts.append(remainder)
                break
            readable, _, _ = select.select([stdout_fd], [], [], min(remaining, 0.25))
            if readable:
                try:
                    chunk = os.read(stdout_fd, 65536)
                    if chunk:
                        output_parts.append(chunk)
                except BlockingIOError:
                    pass
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()

    output = b"".join(output_parts).decode("utf-8", "replace")
    args.log.parent.mkdir(parents=True, exist_ok=True)
    args.log.write_text(output, encoding="utf-8")
    if MARKER in output:
        missing = [marker for marker in REQUIRED_MARKERS if marker not in output]
        if missing:
            print(
                f"RIBON-RPI-SMOKE-FAIL: missing marker {missing[0]}; log={args.log}",
                file=sys.stderr,
            )
            return 1
        print("RIBON-RPI-SMOKE-OK")
        return 0
    if timed_out:
        print(f"RIBON-RPI-SMOKE-FAIL: timeout without marker; log={args.log}", file=sys.stderr)
        return 1
    print(
        f"RIBON-RPI-SMOKE-FAIL: marker not found; exit={return_code}; log={args.log}",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
