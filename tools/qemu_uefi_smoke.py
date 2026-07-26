#!/usr/bin/env python3
"""Run the Ribon UEFI application under QEMU and look for its smoke marker."""

from __future__ import annotations

import argparse
import pathlib
import select
import subprocess
import sys
import time


MARKER = "PARUS-FIXTURE-ENTRY-OK"
REQUIRED_MARKERS = [
    "RIBON-UEFI-KERNEL-LOAD-START",
    "RIBON-UEFI-KERNEL-SEGMENT-RUNTIME=",
    "RIBON-UEFI-KERNEL-RUNTIME-ENTRY=",
    "RIBON-UEFI-KERNEL-LOAD-OK",
    "RIBON-UEFI-FINAL-MEMORY-MAP",
    "RIBON-UEFI-FINAL-MEMORY-MAP-DESCRIPTORS=",
    "RIBON-UEFI-RPH1-SIZE=",
    "RIBON-UEFI-RPH1-SECTIONS=",
    "RIBON-UEFI-RPH1-REBUILD-ATTEMPT=",
    "RIBON-UEFI-JUMP-ENTRY=",
    "RIBON-UEFI-HANDOFF=",
    "RIBON-UEFI-DIAG-STAGE=exit-boot-services",
    "RIBON-UEFI-EXIT-BOOT-SERVICES-START",
    MARKER,
]
FAILURE_MARKERS = [
    "UEFI Interactive Shell",
    "startup.nsh",
    "Shell>",
    "Exception Type",
    "General Protection",
    "AMD64-EXCEPTION",
    "AMD64-DOUBLE-FAULT",
    "AARCH64-EXCEPTION:",
    "AMD64-MMU-FAIL:",
    "AMD64-DESCRIPTOR-FAIL:",
    "RIBON-UEFI-BOOT-VOLUME-FAIL",
    "RIBON-UEFI-KERNEL-READ-FAIL",
    "RIBON-UEFI-MEMORY-MAP-FAIL",
    "RIBON-UEFI-GOP-FAIL",
    "RIBON-UEFI-PLAN-FAIL",
    "RIBON-UEFI-LOADER-STATUS=",
    "RIBON-UEFI-KERNEL-ALLOC-FAIL=",
    "RIBON-UEFI-DIRECT-HIGH-STATUS=",
    "RIBON-UEFI-PLAN-STATUS=",
    "PARUS-FIXTURE-ENTRY-ABI-FAIL",
    "XIBALBA-FAIL:",
    "EARLY_BOOT-FAIL:",
    "HIGHER-HALF-CONTRACT-FAIL:",
    "EARLY_MEMORY-FAIL:",
    "PMM-FAIL:",
    "VMM-FAIL:",
    "KHEAP-FAIL:",
    "CLOCK-FAIL:",
    "IRQ-FAIL:",
    "KMAIN-FAIL:",
    "BOOT-MSGBOX-FAIL:",
    "AUDIT-FAIL:",
    "KCONSOLE-FAIL:",
    "STORAGE-EXECUTOR-FAIL:",
    "EXECUTOR-FAIL:",
    "KCG-FAIL:",
    "USER-JUMP-FAIL:",
    "DEVICE-FAIL:",
    "DISPLAY-FAIL:",
]


def build_qemu_command(
    *,
    arch: str,
    qemu: str,
    firmware: pathlib.Path,
    esp: pathlib.Path,
    memory: str,
    machine: str | None = None,
    cpu: str | None = None,
    vars_path: pathlib.Path | None = None,
    storage_media: str = "none",
    storage_size: int = 1048576,
) -> list[str]:
    def append_storage_candidate(command: list[str]) -> list[str]:
        if storage_media == "none":
            return command
        if storage_media == "null-co":
            command.extend(
                [
                    "-blockdev",
                    f"driver=null-co,node-name=storage0,size={storage_size},read-zeroes=on",
                    "-device",
                    "virtio-blk-pci,drive=storage0,disable-legacy=on,bootindex=9",
                ]
            )
            return command
        raise ValueError(f"unsupported storage media: {storage_media}")

    if arch == "x86_64":
        return append_storage_candidate([
            qemu,
            "-machine",
            machine if machine is not None else "q35",
            "-m",
            memory,
            "-no-reboot",
            "-monitor",
            "none",
            "-serial",
            "stdio",
            "-display",
            "none",
            "-vga",
            "std",
            "-drive",
            f"if=pflash,format=raw,readonly=on,file={firmware}",
            "-drive",
            f"format=raw,file=fat:rw:{esp}",
            "-boot",
            "order=d",
        ])
    if arch == "aarch64":
        if vars_path is None:
            raise ValueError("AArch64 UEFI smoke requires a writable AAVMF vars image")
        return append_storage_candidate([
            qemu,
            "-machine",
            machine if machine is not None else "virt,gic-version=2",
            "-cpu",
            cpu if cpu is not None else "cortex-a72",
            "-m",
            memory,
            "-no-reboot",
            "-monitor",
            "none",
            "-serial",
            "stdio",
            "-display",
            "none",
            "-drive",
            f"if=pflash,format=raw,readonly=on,file={firmware}",
            "-drive",
            f"if=pflash,format=raw,file={vars_path}",
            "-drive",
            f"file=fat:rw:{esp},format=raw,if=none,id=hd0",
            "-device",
            "virtio-blk-device,drive=hd0,bootindex=0",
        ])
    raise ValueError(f"unsupported UEFI smoke arch: {arch}")


def log_tail(output: str, line_count: int) -> str:
    if line_count == 0:
        return ""
    lines = output.splitlines()
    return "\n".join(lines[-line_count:])


def print_diagnostic(kind: str, output: str, log: pathlib.Path, *, detail: str = "") -> None:
    suffix = f": {detail}" if detail else ""
    print(f"RIBON-UEFI-SMOKE-DIAG={kind}{suffix}; log={log}", file=sys.stderr)
    tail = log_tail(output, 80)
    if tail:
        print("RIBON-UEFI-SMOKE-LOG-TAIL-BEGIN", file=sys.stderr)
        print(tail, file=sys.stderr)
        print("RIBON-UEFI-SMOKE-LOG-TAIL-END", file=sys.stderr)


def first_marker_position(output: str, markers: list[str]) -> tuple[int, str] | None:
    found: list[tuple[int, str]] = []
    for marker in markers:
        position = output.find(marker)
        if position >= 0:
            found.append((position, marker))
    if not found:
        return None
    return min(found, key=lambda item: item[0])


def observed_failure_markers(
    output: str,
    failure_markers: list[str],
    *,
    before: int | None = None,
) -> list[str]:
    observed: list[str] = []
    spans: list[tuple[int, int]] = []
    for marker in failure_markers:
        position = output.find(marker)
        if position < 0 or (before is not None and position >= before):
            continue
        marker_end = position + len(marker)
        if any(start <= position and marker_end <= end for start, end in spans):
            continue
        observed.append(marker)
        spans.append((position, marker_end))
    return observed


def missing_required_markers(output: str, required_markers: list[str]) -> list[str]:
    return [marker for marker in required_markers if marker not in output]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--arch", choices=("x86_64", "aarch64"), default="x86_64")
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--firmware", required=True)
    parser.add_argument("--vars", type=pathlib.Path, default=None)
    parser.add_argument("--esp", required=True, type=pathlib.Path)
    parser.add_argument("--log", required=True, type=pathlib.Path)
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--memory", default="256M")
    parser.add_argument("--machine", default=None)
    parser.add_argument("--cpu", default=None)
    parser.add_argument("--storage-media", choices=("none", "null-co"), default="none")
    parser.add_argument("--storage-size", type=int, default=1048576)
    parser.add_argument("--marker", default=MARKER)
    parser.add_argument("--require-marker", action="append", default=None)
    parser.add_argument("--fail-marker", action="append", default=[])
    args = parser.parse_args()

    if args.firmware == "":
        print("RIBON-UEFI-SMOKE-SKIP: missing firmware path", file=sys.stderr)
        return 77
    firmware = pathlib.Path(args.firmware)
    if not firmware.exists():
        print(f"RIBON-UEFI-SMOKE-SKIP: missing firmware: {firmware}", file=sys.stderr)
        return 77
    if args.vars is not None and not args.vars.exists():
        print(f"RIBON-UEFI-SMOKE-SKIP: missing firmware vars image: {args.vars}", file=sys.stderr)
        return 77
    if not args.esp.exists():
        print(f"RIBON-UEFI-SMOKE-FAIL: missing ESP directory: {args.esp}", file=sys.stderr)
        return 1

    try:
        command = build_qemu_command(
            arch=args.arch,
            qemu=args.qemu,
            firmware=firmware,
            esp=args.esp,
            memory=args.memory,
            machine=args.machine,
            cpu=args.cpu,
            vars_path=args.vars,
            storage_media=args.storage_media,
            storage_size=args.storage_size,
        )
    except ValueError as error:
        print(f"RIBON-UEFI-SMOKE-FAIL: {error}", file=sys.stderr)
        return 1

    output_parts: list[str] = []
    timed_out = False
    return_code: int | None = None
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        bufsize=0,
    )
    assert process.stdout is not None
    deadline = time.monotonic() + args.timeout
    target_marker = args.marker
    failure_markers = FAILURE_MARKERS + args.fail_marker
    try:
        while True:
            output = "".join(output_parts)
            target_position = output.find(target_marker)
            first_failure = first_marker_position(output, failure_markers)
            if target_position >= 0 and (first_failure is None or target_position < first_failure[0]):
                process.terminate()
                try:
                    process.wait(timeout=2.0)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                break
            if first_failure is not None and (target_position < 0 or first_failure[0] < target_position):
                process.terminate()
                try:
                    process.wait(timeout=2.0)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                break
            return_code = process.poll()
            if return_code is not None:
                remainder = process.stdout.read()
                if remainder:
                    output_parts.append(remainder)
                break
            remaining = deadline - time.monotonic()
            if remaining <= 0.0:
                timed_out = True
                process.kill()
                remainder = process.communicate()[0]
                if remainder:
                    output_parts.append(remainder)
                break
            readable, _, _ = select.select([process.stdout], [], [], min(remaining, 0.25))
            if readable:
                char = process.stdout.read(1)
                if char:
                    output_parts.append(char)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()

    output = "".join(output_parts)
    args.log.parent.mkdir(parents=True, exist_ok=True)
    args.log.write_text(output, encoding="utf-8")
    required_markers = args.require_marker if args.require_marker is not None else REQUIRED_MARKERS
    target_index = output.find(target_marker)
    missing_markers = missing_required_markers(output, required_markers)
    observed_failures = observed_failure_markers(
        output,
        failure_markers,
        before=target_index if target_index >= 0 else None,
    )
    if observed_failures:
        joined = ", ".join(observed_failures)
        print(f"RIBON-UEFI-SMOKE-FAIL: failure markers: {joined}; log={args.log}", file=sys.stderr)
        print_diagnostic("failure-marker", output, args.log, detail=joined)
        return 1
    if not missing_markers:
        print("RIBON-UEFI-SMOKE-OK")
        return 0
    if timed_out:
        print(f"RIBON-UEFI-SMOKE-FAIL: timeout without marker; log={args.log}", file=sys.stderr)
        print_diagnostic("timeout", output, args.log, detail=f"target={target_marker}")
        return 1
    if missing_markers:
        joined = ", ".join(missing_markers)
        print(
            f"RIBON-UEFI-SMOKE-FAIL: missing markers: {joined}; log={args.log}",
            file=sys.stderr,
        )
        print_diagnostic("missing-marker", output, args.log, detail=missing_markers[0])
        return 1
    print(
        f"RIBON-UEFI-SMOKE-FAIL: marker not found; exit={return_code}; log={args.log}",
        file=sys.stderr,
    )
    print_diagnostic("qemu-exit", output, args.log, detail=f"exit={return_code}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
