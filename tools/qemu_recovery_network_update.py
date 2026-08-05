#!/usr/bin/env python3
"""Fetch, install, reopen, and preserve D05 q35 UEFI bounded-TFTP evidence."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys


CAPABILITY_MARKERS = {
    b"RIBON-D05-UEFI-PXE-TFTP-CAPABILITY-OK": "uefi-pxe-base-code-tftp",
    b"RIBON-D05-UEFI-SNP-TFTP-CAPABILITY-OK": "uefi-snp-bounded-tftp",
}
FETCH_MARKERS = (
    b"RIBON-D05-NETWORK-MANIFEST-FETCHED",
    b"RIBON-D05-NETWORK-SIGNATURE-FETCHED",
    b"RIBON-D05-NETWORK-BUNDLE-FETCHED",
)
FAIL_MARKER = b"RIBON-D05-NETWORK-UPDATE-FAIL"


def load_supervisor():
    """Reuse the D03 process-group supervisor without sharing target logic."""

    path = Path(__file__).with_name("qemu_update_install.py")
    spec = importlib.util.spec_from_file_location("ribon_d05_supervisor", path)
    if spec is None or spec.loader is None:
        raise ValueError("cannot load qemu_update_install.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.FAIL_MARKER = FAIL_MARKER
    return module


def sha256(path: Path) -> str:
    """Hash one exact artifact."""

    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tree_digest(root: Path) -> str:
    """Hash one immutable firmware-visible directory tree."""

    digest = hashlib.sha256()
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        if path.relative_to(root).as_posix() == "NvVars":
            continue
        name = path.relative_to(root).as_posix().encode("utf-8")
        digest.update(len(name).to_bytes(4, "little"))
        digest.update(name)
        digest.update(bytes.fromhex(sha256(path)))
    return digest.hexdigest()


def command(
    args: argparse.Namespace,
    pflash: Path,
    disk: Path,
) -> list[str]:
    """Build one q35 command with exactly one restricted user-mode TFTP NIC."""

    return [
        args.qemu,
        "-machine", "q35,accel=tcg",
        "-m", "256M",
        "-display", "none",
        "-serial", "stdio",
        "-monitor", "none",
        "-no-reboot",
        "-no-shutdown",
        "-drive", f"if=pflash,format=raw,file={pflash}",
        "-drive", f"format=raw,file=fat:rw:{args.esp}",
        "-drive", f"if=virtio,format=raw,cache=directsync,file={disk}",
        "-netdev", f"user,id=ribonnet,restrict=on,tftp={args.tftp_root}",
        "-device", "e1000,netdev=ribonnet",
    ]


def inspect(
    args: argparse.Namespace,
    disk: Path,
    output: Path,
    active_sha256: str,
) -> dict[str, object]:
    """Run the independent D04 journal and payload inspector."""

    completed = subprocess.run(
        [
            sys.executable, str(args.inspector),
            "--disk", str(disk),
            "--manifest", str(args.manifest),
            "--expected-active-sha256", active_sha256,
            "--output", str(output),
        ],
        check=False, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    if completed.returncode != 0:
        raise ValueError(completed.stdout.decode("utf-8", errors="replace"))
    return json.loads(output.read_text(encoding="utf-8"))


def require_markers(output: bytes, boot: str) -> str:
    """Require one capability and one receipt for every fetched object."""

    capability_counts = {
        transport: output.count(marker)
        for marker, transport in CAPABILITY_MARKERS.items()
    }
    selected = [
        transport for marker, transport in CAPABILITY_MARKERS.items()
        if output.count(marker) == 1
    ]
    if len(selected) != 1 or any(
        output.count(marker) > 1 for marker in CAPABILITY_MARKERS
    ):
        tail = output[-2048:].decode("utf-8", errors="replace")
        raise ValueError(
            f"{boot} does not have one exact transport marker: "
            f"counts={capability_counts}, tail={tail!r}"
        )
    for marker in FETCH_MARKERS:
        if output.count(marker) != 1:
            raise ValueError(
                f"{boot} has {output.count(marker)} copies of {marker!r}"
            )
    return selected[0]


def main() -> int:
    """Run network install and reopen with exact hashes and cleanup receipts."""

    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--esp", type=Path, required=True)
    parser.add_argument("--disk", type=Path, required=True)
    parser.add_argument("--boot-app", type=Path, required=True)
    parser.add_argument("--tftp-root", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--envelope", type=Path, required=True)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--product-manifest", type=Path, required=True)
    parser.add_argument("--fixture-provenance", type=Path, required=True)
    parser.add_argument("--inspector", type=Path, required=True)
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()
    supervisor = load_supervisor()
    try:
        args.results.mkdir(parents=True, exist_ok=True)
        provenance = json.loads(args.fixture_provenance.read_text(encoding="utf-8"))
        expected_active = provenance.get("active_slot_sha256")
        expected_disk = (
            provenance.get("artifacts", {})
            .get("update-disk.raw", {})
            .get("sha256")
        )
        if (
            provenance.get("schema") != "ribon-qemu-update-fixture-v1"
            or not isinstance(expected_active, str)
            or len(expected_active) != 64
            or not isinstance(expected_disk, str)
            or len(expected_disk) != 64
            or sha256(args.disk) != expected_disk
        ):
            raise ValueError("network fixture provenance is incomplete")
        for path in (args.manifest, args.envelope, args.bundle):
            if path.parent != args.tftp_root or not path.is_file():
                raise ValueError("TFTP object is outside the exact fixture root")
        initial_esp = tree_digest(args.esp)
        initial_tftp = tree_digest(args.tftp_root)
        runtime_disk = args.results / "network-update-runtime.raw"
        shutil.copyfile(args.disk, runtime_disk)
        input_disk_sha = sha256(runtime_disk)
        first_pflash = args.results / "pflash-network-install.fd"
        second_pflash = args.results / "pflash-network-reopen.fd"
        shutil.copyfile(args.firmware, first_pflash)
        shutil.copyfile(args.firmware, second_pflash)
        first_command = command(args, first_pflash, runtime_disk)
        second_command = command(args, second_pflash, runtime_disk)
        first_output, first = supervisor.boot_once(
            first_command,
            b"RIBON-D05-NETWORK-INSTALLED-PENDING",
            args.timeout,
        )
        first_log = args.results / "qemu-network-install.log"
        first_log.write_bytes(first_output)
        first_transport = require_markers(first_output, "install")
        if (
            first["outcome"] != "passed"
            or first["forced_kill"]
            or not first["cleanup_complete"]
        ):
            raise ValueError(f"network install boot failed: {first}")
        first_inspection = inspect(
            args, runtime_disk, args.results / "disk-after-network-install.json",
            expected_active,
        )
        second_output, second = supervisor.boot_once(
            second_command,
            b"RIBON-D05-NETWORK-REOPEN-PENDING",
            args.timeout,
        )
        second_log = args.results / "qemu-network-reopen.log"
        second_log.write_bytes(second_output)
        second_transport = require_markers(second_output, "reopen")
        if first_transport != second_transport:
            raise ValueError("firmware transport selection changed across reboot")
        if (
            second["outcome"] != "passed"
            or second["forced_kill"]
            or not second["cleanup_complete"]
        ):
            raise ValueError(f"network reopen boot failed: {second}")
        second_inspection = inspect(
            args, runtime_disk, args.results / "disk-after-network-reopen.json",
            expected_active,
        )
        if initial_esp != tree_digest(args.esp):
            raise ValueError("network recovery ESP changed")
        if initial_tftp != tree_digest(args.tftp_root):
            raise ValueError("read-only TFTP fixture changed")
        report = {
            "schema": "ribon-qemu-recovery-network-update-result-v1",
            "source_revision": args.source_revision,
            "qemu_version": supervisor.qemu_version(args.qemu),
            "transport": first_transport,
            "network_enabled": True,
            "network_mode": "restricted-qemu-user-tftp",
            "commands": [first_command, second_command],
            "first_boot": first,
            "second_boot": second,
            "process_group_cleanup_complete": bool(
                first["cleanup_complete"] and second["cleanup_complete"]
            ),
            "forced_kill_count": int(bool(first["forced_kill"])) +
                int(bool(second["forced_kill"])),
            "active_slot_unchanged": bool(
                first_inspection["active_slot_unchanged"] and
                second_inspection["active_slot_unchanged"]
            ),
            "pending_generation": second_inspection["journal_generation"],
            "installed_component_count": len(
                second_inspection["installed_components"]
            ),
            "artifacts": {
                "disk": {
                    "path": str(runtime_disk),
                    "sha256_before": input_disk_sha,
                    "sha256_after": sha256(runtime_disk),
                },
                "firmware": {
                    "path": str(args.firmware), "sha256": sha256(args.firmware)
                },
                "boot_app": {
                    "path": str(args.boot_app), "sha256": sha256(args.boot_app)
                },
                "product_manifest": {
                    "path": str(args.product_manifest),
                    "sha256": sha256(args.product_manifest),
                },
                "manifest": {
                    "path": str(args.manifest), "sha256": sha256(args.manifest)
                },
                "signature_envelope": {
                    "path": str(args.envelope), "sha256": sha256(args.envelope)
                },
                "bundle": {
                    "path": str(args.bundle), "sha256": sha256(args.bundle)
                },
                "fixture_provenance": {
                    "path": str(args.fixture_provenance),
                    "sha256": sha256(args.fixture_provenance),
                },
                "esp": {"path": str(args.esp), "tree_sha256": initial_esp},
                "tftp_root": {
                    "path": str(args.tftp_root), "tree_sha256": initial_tftp
                },
                "serial_install": {
                    "path": str(first_log), "sha256": sha256(first_log)
                },
                "serial_reopen": {
                    "path": str(second_log), "sha256": sha256(second_log)
                },
            },
        }
        output = args.results / "qemu-recovery-network-update.json"
        output.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        if (
            not report["process_group_cleanup_complete"]
            or report["forced_kill_count"] != 0
            or not report["active_slot_unchanged"]
            or report["pending_generation"] != 4
            or report["installed_component_count"] != 2
        ):
            raise ValueError("network update result closure is incomplete")
        print(
            "RIBON-D05-QEMU-RECOVERY-NETWORK-UPDATE-OK "
            f"transport={first_transport} fetches=6 pending-generation=4"
        )
        return 0
    except (OSError, ValueError, subprocess.SubprocessError, json.JSONDecodeError) as error:
        print(f"qemu-recovery-network-update: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
