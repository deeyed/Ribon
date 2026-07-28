#!/usr/bin/env python3
"""Run a bounded Ribon target smoke and preserve payload-aware evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time


TARGET_MARKERS = {
    "aarch64-virt-raw-fdt": (
        b"RIBON-R4-RAW-FDT-ENTRY",
        b"RIBON-R4-FDT-ACCEPTED",
        b"RIBON-R4-PRODUCT-GRAPH-OK",
        b"RIBON-R4-PARUS-RPH1-OK",
        b"RIBON-R4-PAYLOAD-LOADED",
        b"RIBON-R4-RAW-FDT-TRANSFER",
    ),
    "riscv64-virt-opensbi": (
        b"RIBON-R4-RAW-FDT-ENTRY",
        b"RIBON-R4-FDT-ACCEPTED",
        b"RIBON-R4-PRODUCT-GRAPH-OK",
        b"RIBON-R4-PARUS-RPH1-OK",
        b"RIBON-R4-PAYLOAD-LOADED",
        b"RIBON-R4-RAW-FDT-TRANSFER",
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
    ),
}
FIXTURE_MARKERS = (
    b"PARUS-FIXTURE-ENTRY-OK",
    b"PARUS-FIXTURE-ENTRY-ABI-FAIL",
)


def sha256_file(path: Path) -> str:
    """Return the SHA-256 identity of one immutable file."""
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_tree(path: Path) -> str:
    """Return a stable name-and-content digest for a directory tree."""
    digest = hashlib.sha256()
    for entry in sorted(item for item in path.rglob("*") if item.is_file()):
        digest.update(entry.relative_to(path).as_posix().encode("utf-8"))
        digest.update(b"\0")
        digest.update(bytes.fromhex(sha256_file(entry)))
    return digest.hexdigest()


def artifact_sha256(path: Path) -> str:
    """Hash either a file artifact or a composed directory artifact."""
    return sha256_tree(path) if path.is_dir() else sha256_file(path)


def observed_payload_class(path: Path) -> str:
    """Distinguish generated fixture payloads from external ELF payloads."""
    prefix = path.read_bytes()
    if not prefix.startswith(b"\x7fELF"):
        return "invalid"
    if any(marker in prefix for marker in FIXTURE_MARKERS):
        return "fixture"
    return "kernel"


def command_for(args: argparse.Namespace) -> list[str]:
    """Build the selected QEMU command without launching it."""
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
    if args.target == "riscv64-virt-opensbi":
        if args.image is None or args.firmware is None:
            raise ValueError("--image and --firmware are required")
        return [
            args.qemu,
            "-machine", "virt",
            "-m", "256M",
            "-nographic",
            "-monitor", "none",
            "-net", "none",
            "-no-reboot",
            "-no-shutdown",
            "-bios", str(args.firmware),
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


def qemu_version(binary: str) -> str:
    """Capture a bounded first-line QEMU version string."""
    try:
        completed = subprocess.run(
            [binary, "--version"],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=2,
        )
    except (OSError, subprocess.TimeoutExpired):
        return "unavailable"
    lines = completed.stdout.decode("utf-8", errors="replace").splitlines()
    return lines[0] if lines else "unavailable"


def process_group_alive(process_group: int) -> bool:
    """Report whether the launched process group still exists."""
    try:
        os.killpg(process_group, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def required_markers(args: argparse.Namespace) -> tuple[bytes, ...]:
    """Select fixture or actual-payload evidence without kernel policy."""
    markers = TARGET_MARKERS[args.target]
    if args.expected_payload_class == "fixture":
        markers += (FIXTURE_MARKERS[0],)
    markers += tuple(marker.encode("utf-8") for marker in args.required_marker)
    return markers


def marker_observations(
    output: bytes,
    markers: tuple[bytes, ...],
) -> tuple[list[dict[str, object]], str | None]:
    """Record exact marker count/order and the first missing or duplicate marker."""
    observations = []
    previous_offset = -1
    first_divergence = None
    for marker in markers:
        count = output.count(marker)
        offset = output.find(marker)
        observations.append(
            {
                "marker": marker.decode("utf-8"),
                "count": count,
                "offset": offset,
            }
        )
        if first_divergence is None and count == 0:
            first_divergence = f"missing:{marker.decode('utf-8')}"
        elif first_divergence is None and count != 1:
            first_divergence = f"duplicate:{marker.decode('utf-8')}"
        elif first_divergence is None and offset <= previous_offset:
            first_divergence = f"out-of-order:{marker.decode('utf-8')}"
        if offset >= 0:
            previous_offset = offset
    return observations, first_divergence


def write_result(path: Path, report: dict[str, object]) -> None:
    """Write one canonical, machine-readable result document."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def main() -> int:
    """Validate payload identity, supervise QEMU, and publish evidence."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=sorted(TARGET_MARKERS), required=True)
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--image", type=Path)
    parser.add_argument("--esp", type=Path)
    parser.add_argument("--firmware", type=Path)
    parser.add_argument("--payload", type=Path, required=True)
    parser.add_argument("--product-manifest", type=Path)
    parser.add_argument(
        "--expected-payload-class",
        choices=("fixture", "kernel"),
        required=True,
    )
    parser.add_argument("--expected-payload-sha256")
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--required-marker", action="append", default=[])
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--result", type=Path, required=True)
    args = parser.parse_args()

    command = command_for(args)
    composed_path = args.image if args.image is not None else args.esp
    assert composed_path is not None
    payload_hash = artifact_sha256(args.payload)
    composed_hash = artifact_sha256(composed_path)
    payload_class = observed_payload_class(args.payload)
    markers = required_markers(args)
    started = time.monotonic()
    output = bytearray()
    outcome = "preflight-failure"
    terminal = "not-launched"
    timed_out = False
    forced_kill = False
    launched = False
    cleanup_complete = True
    process_group_alive_after_cleanup = False

    preflight_error = None
    if args.expected_payload_sha256 not in (None, payload_hash):
        preflight_error = "payload-hash-mismatch"
    elif payload_class != args.expected_payload_class:
        preflight_error = "payload-class-mismatch"

    if preflight_error is None:
        process = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        launched = True
        outcome = "timeout"
        terminal = "running"
        try:
            assert process.stdout is not None
            os.set_blocking(process.stdout.fileno(), False)
            while time.monotonic() - started < args.timeout:
                chunk = process.stdout.read()
                if chunk:
                    output += chunk
                    if b"RIBON-R4-" in output and b"-FAIL" in output:
                        outcome = "target-failure"
                        terminal = "target-failure"
                        break
                    if b"PARUS-FIXTURE-ENTRY-ABI-FAIL" in output:
                        outcome = "payload-abi-failure"
                        terminal = "payload-abi-failure"
                        break
                    observations, divergence = marker_observations(
                        bytes(output),
                        markers,
                    )
                    if divergence is None and all(
                        item["count"] == 1 for item in observations
                    ):
                        outcome = "passed"
                        terminal = "required-evidence-observed"
                        break
                if process.poll() is not None:
                    outcome = "early-exit"
                    terminal = "process-exit"
                    break
                time.sleep(0.02)
            else:
                timed_out = True
                terminal = "timeout"
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    forced_kill = True
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=2)
            assert process.stdout is not None
            tail = process.stdout.read()
            if tail:
                output += tail
            process_group_alive_after_cleanup = process_group_alive(process.pid)
            cleanup_complete = (
                process.poll() is not None
                and not process_group_alive_after_cleanup
            )
    else:
        outcome = preflight_error
        terminal = "preflight-rejected"

    args.log.parent.mkdir(parents=True, exist_ok=True)
    args.log.write_bytes(output)
    observations, first_divergence = marker_observations(bytes(output), markers)
    if preflight_error is not None:
        first_divergence = preflight_error
    if outcome == "passed" and first_divergence is not None:
        outcome = "evidence-failure"
        terminal = "marker-invariant-failure"

    payload_hash_after = artifact_sha256(args.payload)
    if payload_hash_after != payload_hash:
        outcome = "payload-mutated"
        terminal = "artifact-identity-failure"
        first_divergence = "payload-mutated-during-run"

    report = {
        "schema": "ribon-qemu-payload-evidence-v1",
        "schema_version": 1,
        "target": args.target,
        "expected_product_class": (
            "fixture-smoke"
            if args.expected_payload_class == "fixture"
            else "external-kernel-boot"
        ),
        "observed_payload_class": payload_class,
        "source_revision": args.source_revision,
        "payload": {
            "path": str(args.payload),
            "sha256": payload_hash,
            "sha256_after_run": payload_hash_after,
            "immutable": payload_hash == payload_hash_after,
        },
        "product_manifest": (
            {
                "path": str(args.product_manifest),
                "sha256": artifact_sha256(args.product_manifest),
                "product_id": json.loads(
                    args.product_manifest.read_text(encoding="utf-8")
                ).get("product_id"),
            }
            if args.product_manifest is not None
            else None
        ),
        "composed_artifact": {
            "path": str(composed_path),
            "sha256": composed_hash,
        },
        "firmware": (
            {
                "path": str(args.firmware),
                "sha256": artifact_sha256(args.firmware),
            }
            if args.firmware is not None
            else None
        ),
        "qemu": {
            "version": qemu_version(args.qemu),
            "command": command,
        },
        "timeout": {
            "seconds": args.timeout,
            "occurred": timed_out,
        },
        "terminal": terminal,
        "cleanup": {
            "launched": launched,
            "complete": cleanup_complete,
            "forced_kill": forced_kill,
            "process_group_alive_after_cleanup": (
                process_group_alive_after_cleanup
            ),
        },
        "elapsed_seconds": round(time.monotonic() - started, 3),
        "raw_serial": {
            "path": str(args.log),
            "sha256": sha256_file(args.log),
            "preserved": True,
        },
        "required_markers": [
            marker.decode("utf-8") for marker in markers
        ],
        "marker_observations": observations,
        "first_divergence": first_divergence,
        "outcome": outcome,
    }
    write_result(args.result, report)
    if outcome != "passed" or not cleanup_complete or forced_kill:
        if output:
            print(output.decode("utf-8", errors="replace"))
        print(f"RIBON-QEMU-EVIDENCE-FAIL {outcome}")
        return 1
    print(f"RIBON-QEMU-EVIDENCE-OK {args.target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
