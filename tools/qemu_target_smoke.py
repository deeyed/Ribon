#!/usr/bin/env python3
"""Run a bounded Ribon target smoke and preserve machine-readable evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time


MARKERS = {
    "aarch64-virt-raw-fdt": (
        b"RIBON-R4-RAW-FDT-ENTRY",
        b"RIBON-R4-FDT-ACCEPTED",
        b"RIBON-R4-PRODUCT-GRAPH-OK",
        b"RIBON-R4-PARUS-RPH1-OK",
        b"RIBON-R4-PAYLOAD-LOADED",
        b"RIBON-R4-RAW-FDT-TRANSFER",
        b"PARUS-FIXTURE-ENTRY-OK",
    ),
    "x86_64-uefi": (
        b"RIBON-R4-UEFI-ENTRY",
        b"RIBON-R8-UEFI-CONFIG-OK",
        b"RIBON-R8-UEFI-ESP-PAYLOAD-OK",
        b"RIBON-R4-UEFI-PAYLOAD-LOADED",
        b"RIBON-R4-UEFI-MEMORY-MAP",
        b"RIBON-R4-UEFI-PRODUCT-GRAPH-OK",
        b"RIBON-R4-UEFI-FINAL-MAP-RPH1-OK",
        b"RIBON-R4-UEFI-EXIT-BOOT-SERVICES-OK",
        b"RIBON-R4-UEFI-TRANSFER",
        b"PARUS-FIXTURE-ENTRY-OK",
    ),
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def command_for(args: argparse.Namespace) -> list[str]:
    if args.target == "aarch64-virt-raw-fdt":
        if args.image is None:
            raise ValueError("--image is required")
        return [
            args.qemu,
            "-machine", "virt",
            "-cpu", "cortex-a72",
            "-m", "256M",
            "-nographic",
            "-monitor", "none",
            "-net", "none",
            "-no-reboot",
            "-no-shutdown",
            "-kernel", str(args.image),
        ]
    if args.esp is None or args.firmware is None:
        raise ValueError("--esp and --firmware are required")
    return [
        args.qemu,
        "-machine", "q35",
        "-m", "256M",
        "-display", "none",
        "-serial", "stdio",
        "-monitor", "none",
        "-net", "none",
        "-no-reboot",
        "-no-shutdown",
        "-drive", f"if=pflash,format=raw,readonly=on,file={args.firmware}",
        "-drive", f"format=raw,file=fat:rw:{args.esp}",
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=sorted(MARKERS), required=True)
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--image", type=Path)
    parser.add_argument("--esp", type=Path)
    parser.add_argument("--firmware", type=Path)
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--result", type=Path, required=True)
    args = parser.parse_args()
    command = command_for(args)
    started = time.monotonic()
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    output = bytearray()
    outcome = "timeout"
    try:
        assert process.stdout is not None
        os.set_blocking(process.stdout.fileno(), False)
        while time.monotonic() - started < args.timeout:
            chunk = process.stdout.read()
            if chunk:
                output += chunk
                if b"RIBON-R4-" in output and b"-FAIL" in output:
                    outcome = "target-failure"
                    break
                if b"PARUS-FIXTURE-ENTRY-ABI-FAIL" in output:
                    outcome = "payload-abi-failure"
                    break
                if all(marker in output for marker in MARKERS[args.target]):
                    outcome = "passed"
                    break
            if process.poll() is not None:
                outcome = "early-exit"
                break
            time.sleep(0.02)
    finally:
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=2)
        assert process.stdout is not None
        tail = process.stdout.read()
        if tail:
            output += tail

    args.log.parent.mkdir(parents=True, exist_ok=True)
    args.log.write_bytes(output)
    image_path = args.image
    if image_path is None:
        image_path = args.esp / "EFI" / "BOOT" / "BOOTX64.EFI"
    report = {
        "command": command,
        "elapsed_seconds": round(time.monotonic() - started, 3),
        "image": str(image_path),
        "image_sha256": sha256(image_path),
        "log": str(args.log),
        "log_sha256": sha256(args.log),
        "markers": [marker.decode("ascii") for marker in MARKERS[args.target]],
        "outcome": outcome,
        "target": args.target,
    }
    args.result.parent.mkdir(parents=True, exist_ok=True)
    args.result.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    if outcome != "passed":
        print(output.decode("utf-8", errors="replace"))
        return 1
    print(f"RIBON-R4-QEMU-SMOKE-OK {args.target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
