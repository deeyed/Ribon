#!/usr/bin/env python3
"""Run two supervised q35 UEFI boots and preserve D03 update evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import shutil
import subprocess
import sys
import time


FAIL_MARKER = b"RIBON-D03-UPDATE-FAIL"


def sha256(path: Path) -> str:
    """Hash one exact artifact."""

    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tree_digest(root: Path) -> str:
    """Hash immutable ESP inputs while excluding firmware-owned NvVars output."""

    digest = hashlib.sha256()
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        if path.relative_to(root).as_posix() == "NvVars":
            continue
        relative = path.relative_to(root).as_posix().encode("utf-8")
        digest.update(len(relative).to_bytes(4, "little"))
        digest.update(relative)
        digest.update(bytes.fromhex(sha256(path)))
    return digest.hexdigest()


def process_group_alive(group: int) -> bool:
    """Return whether the exact launched process group still exists."""

    try:
        os.killpg(group, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def qemu_version(binary: str) -> str:
    """Capture a bounded QEMU version line."""

    completed = subprocess.run(
        [binary, "--version"], check=False, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, timeout=3,
    )
    lines = completed.stdout.decode("utf-8", errors="replace").splitlines()
    return lines[0] if lines else "unavailable"


def command(args: argparse.Namespace, pflash: Path) -> list[str]:
    """Build the exact q35 command without network or snapshot semantics."""

    return [
        args.qemu,
        "-machine", "q35,accel=tcg",
        "-m", "256M",
        "-display", "none",
        "-serial", "stdio",
        "-monitor", "none",
        "-net", "none",
        "-no-reboot",
        "-no-shutdown",
        "-drive", f"if=pflash,format=raw,file={pflash}",
        "-drive", f"format=raw,file=fat:rw:{args.esp}",
        "-drive", f"if=virtio,format=raw,cache=directsync,file={args.disk}",
    ]


def boot_once(command_line: list[str], marker: bytes, timeout: float) -> tuple[bytes, dict[str, object]]:
    """Run until one unique terminal marker and clean its process group."""

    output = bytearray()
    started = time.monotonic()
    forced_kill = False
    outcome = "timeout"
    process = subprocess.Popen(
        command_line, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    try:
        assert process.stdout is not None
        os.set_blocking(process.stdout.fileno(), False)
        while time.monotonic() - started < timeout:
            chunk = process.stdout.read()
            if chunk:
                output += chunk
                if FAIL_MARKER in output:
                    outcome = "target-failure"
                    break
                if output.count(marker) == 1:
                    outcome = "passed"
                    break
                if output.count(marker) > 1:
                    outcome = "duplicate-marker"
                    break
            if process.poll() is not None:
                outcome = "early-exit"
                break
            time.sleep(0.02)
    finally:
        if process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except (ProcessLookupError, PermissionError):
                pass
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                forced_kill = True
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except (ProcessLookupError, PermissionError):
                    process.kill()
                process.wait(timeout=2)
        assert process.stdout is not None
        tail = process.stdout.read()
        if tail:
            output += tail
    cleanup_complete = process.poll() is not None and not process_group_alive(process.pid)
    receipt = {
        "outcome": outcome,
        "terminal_marker": marker.decode("ascii"),
        "marker_count": output.count(marker),
        "fail_marker_count": output.count(FAIL_MARKER),
        "elapsed_seconds": round(time.monotonic() - started, 6),
        "exit_code": process.returncode,
        "cleanup_complete": cleanup_complete,
        "forced_kill": forced_kill,
        "process_group_alive_after_cleanup": process_group_alive(process.pid),
    }
    return bytes(output), receipt


def run_inspector(
    args: argparse.Namespace,
    expected_active_sha256: str,
    output: Path,
) -> dict[str, object]:
    """Run the separate disk inspector and return its persisted report."""

    completed = subprocess.run(
        [
            sys.executable, str(args.inspector), "--disk", str(args.disk),
            "--manifest", str(args.manifest),
            "--expected-active-sha256", expected_active_sha256,
            "--output", str(output),
        ],
        check=False, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    if completed.returncode != 0:
        raise ValueError(completed.stdout.decode("utf-8", errors="replace"))
    return json.loads(output.read_text(encoding="utf-8"))


def main() -> int:
    """Execute, inspect, reboot, and preserve all exact evidence identities."""

    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--esp", type=Path, required=True)
    parser.add_argument("--disk", type=Path, required=True)
    parser.add_argument("--boot-app", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--envelope", type=Path, required=True)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--product-manifest", type=Path, required=True)
    parser.add_argument("--fixture-provenance", type=Path, required=True)
    parser.add_argument("--inspector", type=Path, required=True)
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=20.0)
    args = parser.parse_args()
    try:
        args.results.mkdir(parents=True, exist_ok=True)
        provenance = json.loads(args.fixture_provenance.read_text(encoding="utf-8"))
        expected_active_sha256 = provenance.get("active_slot_sha256")
        expected_fixture_sha256 = (
            provenance.get("artifacts", {})
            .get("update-disk.raw", {})
            .get("sha256")
        )
        if (
            provenance.get("schema") != "ribon-qemu-update-fixture-v1"
            or not isinstance(expected_active_sha256, str)
            or len(expected_active_sha256) != 64
            or not isinstance(expected_fixture_sha256, str)
            or len(expected_fixture_sha256) != 64
        ):
            raise ValueError("fixture provenance lacks exact disk identities")
        fixture_disk = args.disk
        if sha256(fixture_disk) != expected_fixture_sha256:
            raise ValueError("input fixture disk differs from immutable provenance")
        runtime_disk = args.results / "update-disk-runtime.raw"
        shutil.copyfile(fixture_disk, runtime_disk)
        args.disk = runtime_disk
        initial_disk_sha = sha256(runtime_disk)
        initial_esp_sha = tree_digest(args.esp)
        first_pflash = args.results / "qemu-pflash-install.fd"
        second_pflash = args.results / "qemu-pflash-reopen.fd"
        shutil.copyfile(args.firmware, first_pflash)
        shutil.copyfile(args.firmware, second_pflash)
        first_command = command(args, first_pflash)
        second_command = command(args, second_pflash)
        first_output, first = boot_once(
            first_command, b"RIBON-D03-UPDATE-INSTALLED-VERIFIED", args.timeout
        )
        first_log = args.results / "qemu-install.log"
        first_log.write_bytes(first_output)
        if first["outcome"] != "passed" or first["forced_kill"] or not first["cleanup_complete"]:
            raise ValueError(f"first QEMU boot failed: {first}")
        first_inspection = run_inspector(
            args, expected_active_sha256, args.results / "disk-after-install.json"
        )
        second_output, second = boot_once(
            second_command, b"RIBON-D03-UPDATE-REOPEN-VERIFIED", args.timeout
        )
        second_log = args.results / "qemu-reopen.log"
        second_log.write_bytes(second_output)
        if second["outcome"] != "passed" or second["forced_kill"] or not second["cleanup_complete"]:
            raise ValueError(f"second QEMU boot failed: {second}")
        second_inspection = run_inspector(
            args, expected_active_sha256, args.results / "disk-after-reopen.json"
        )
        final_esp_sha = tree_digest(args.esp)
        if initial_esp_sha != final_esp_sha:
            raise ValueError("read-only ESP tree changed during QEMU execution")
        artifacts = {
            "raw_disk_fixture": {
                "path": str(fixture_disk),
                "sha256": expected_fixture_sha256,
            },
            "raw_disk": {"path": str(args.disk), "sha256_before": initial_disk_sha,
                         "sha256_after": sha256(args.disk)},
            "esp": {"path": str(args.esp), "tree_sha256": initial_esp_sha},
            "bundle": {"path": str(args.bundle), "sha256": sha256(args.bundle)},
            "manifest": {"path": str(args.manifest), "sha256": sha256(args.manifest)},
            "signature_envelope": {"path": str(args.envelope), "sha256": sha256(args.envelope)},
            "firmware": {"path": str(args.firmware), "sha256": sha256(args.firmware)},
            "boot_app": {"path": str(args.boot_app), "sha256": sha256(args.boot_app)},
            "product_manifest": {"path": str(args.product_manifest),
                                 "sha256": sha256(args.product_manifest)},
            "fixture_provenance": {"path": str(args.fixture_provenance),
                                   "sha256": sha256(args.fixture_provenance)},
            "serial_install": {"path": str(first_log), "sha256": sha256(first_log)},
            "serial_reopen": {"path": str(second_log), "sha256": sha256(second_log)},
        }
        nvvars = args.esp / "NvVars"
        if nvvars.is_file():
            artifacts["nvvars"] = {"path": str(nvvars), "sha256": sha256(nvvars)}
        report = {
            "schema": "ribon-qemu-update-install-result-v1",
            "source_revision": args.source_revision,
            "qemu_version": qemu_version(args.qemu),
            "commands": [first_command, second_command],
            "network_enabled": False,
            "first_boot": first,
            "second_boot": second,
            "process_group_cleanup_complete": bool(
                first["cleanup_complete"] and second["cleanup_complete"]
            ),
            "forced_kill_count": int(bool(first["forced_kill"])) + int(bool(second["forced_kill"])),
            "active_slot_unchanged": bool(
                first_inspection["active_slot_unchanged"]
                and second_inspection["active_slot_unchanged"]
            ),
            "installed_component_count": len(first_inspection["installed_components"]),
            "verified_reopen": second_inspection["metadata"]["slots"][1]["state"] == 2,
            "artifacts": artifacts,
        }
        result_path = args.results / "qemu-update-install.json"
        result_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        if (
            not report["process_group_cleanup_complete"]
            or report["forced_kill_count"] != 0
            or not report["active_slot_unchanged"]
            or not report["verified_reopen"]
        ):
            raise ValueError("QEMU result closure is incomplete")
        print("RIBON-D03-QEMU-UPDATE-INSTALL-OK")
        return 0
    except (OSError, ValueError, subprocess.SubprocessError, json.JSONDecodeError) as error:
        print(f"qemu-update-install: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
