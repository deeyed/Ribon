#!/usr/bin/env python3
"""Boot selected D04 crash images through q35 UEFI recovery and reopen them."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys


CASES = (
    "after-staging-commit",
    "after-payload-flush",
    "after-verified-commit",
)
MODE_BYTES = b"RIBON-D04-TXN-V1"


def load_qemu_tool():
    """Load the shared supervised process helper."""

    path = Path(__file__).with_name("qemu_update_install.py")
    spec = importlib.util.spec_from_file_location("ribon_qemu_update_install", path)
    if spec is None or spec.loader is None:
        raise ValueError("cannot load qemu_update_install.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


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
        name = path.relative_to(root).as_posix().encode("utf-8")
        digest.update(len(name).to_bytes(4, "little"))
        digest.update(name)
        digest.update(bytes.fromhex(sha256(path)))
    return digest.hexdigest()


def qemu_command(args: argparse.Namespace, esp: Path, disk: Path, pflash: Path) -> list[str]:
    """Build the exact network-disabled q35 command."""

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
        "-drive", f"format=raw,file=fat:rw:{esp}",
        "-drive", f"if=virtio,format=raw,cache=directsync,file={disk}",
    ]


def inspect(args: argparse.Namespace, disk: Path, output: Path,
            active_sha256: str) -> dict[str, object]:
    """Run the independent D04 disk inspector."""

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


def main() -> int:
    """Recover three crash states, reboot, inspect, and preserve receipts."""

    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--esp-template", type=Path, required=True)
    parser.add_argument("--boot-app", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--product-manifest", type=Path, required=True)
    parser.add_argument("--fixture-provenance", type=Path, required=True)
    parser.add_argument("--coverage", type=Path, required=True)
    parser.add_argument("--case-root", type=Path, required=True)
    parser.add_argument("--inspector", type=Path, required=True)
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=20.0)
    args = parser.parse_args()
    qemu_tool = load_qemu_tool()
    try:
        args.results.mkdir(parents=True, exist_ok=True)
        provenance = json.loads(args.fixture_provenance.read_text(encoding="utf-8"))
        coverage = json.loads(args.coverage.read_text(encoding="utf-8"))
        active_sha256 = provenance.get("active_slot_sha256")
        if (
            not isinstance(active_sha256, str)
            or len(active_sha256) != 64
            or coverage.get("event_count") != 54
            or coverage.get("selected_qemu_cases") != len(CASES)
        ):
            raise ValueError("host fault-model provenance is incomplete")
        case_reports = []
        total_forced_kills = 0
        for name in CASES:
            source_disk = args.case_root / f"{name}.raw"
            case_root = args.results / name
            esp = case_root / "esp"
            disk = case_root / "runtime.raw"
            case_root.mkdir(parents=True, exist_ok=True)
            if esp.exists():
                shutil.rmtree(esp)
            shutil.copytree(args.esp_template, esp)
            mode = esp / "RIBON" / "TRANSACT.V1"
            mode.parent.mkdir(parents=True, exist_ok=True)
            mode.write_bytes(MODE_BYTES)
            shutil.copyfile(source_disk, disk)
            initial_disk_sha = sha256(disk)
            initial_esp_sha = tree_digest(esp)
            install_pflash = case_root / "pflash-recovery.fd"
            reopen_pflash = case_root / "pflash-reopen.fd"
            shutil.copyfile(args.firmware, install_pflash)
            shutil.copyfile(args.firmware, reopen_pflash)
            first_command = qemu_command(args, esp, disk, install_pflash)
            first_output, first = qemu_tool.boot_once(
                first_command, b"RIBON-D04-TRANSACTION-PENDING", args.timeout
            )
            first_log = case_root / "qemu-recovery.log"
            first_log.write_bytes(first_output)
            if (
                first["outcome"] != "passed"
                or first["forced_kill"]
                or not first["cleanup_complete"]
            ):
                raise ValueError(f"{name} recovery boot failed: {first}")
            after_recovery = inspect(
                args, disk, case_root / "disk-after-recovery.json", active_sha256
            )
            second_command = qemu_command(args, esp, disk, reopen_pflash)
            second_output, second = qemu_tool.boot_once(
                second_command,
                b"RIBON-D04-TRANSACTION-REOPEN-PENDING",
                args.timeout,
            )
            second_log = case_root / "qemu-reopen.log"
            second_log.write_bytes(second_output)
            if (
                second["outcome"] != "passed"
                or second["forced_kill"]
                or not second["cleanup_complete"]
            ):
                raise ValueError(f"{name} reopen boot failed: {second}")
            after_reopen = inspect(
                args, disk, case_root / "disk-after-reopen.json", active_sha256
            )
            if initial_esp_sha != tree_digest(esp):
                raise ValueError(f"{name} ESP changed during QEMU execution")
            total_forced_kills += int(bool(first["forced_kill"]))
            total_forced_kills += int(bool(second["forced_kill"]))
            case_reports.append({
                "case": name,
                "input_disk_sha256": initial_disk_sha,
                "output_disk_sha256": sha256(disk),
                "esp_tree_sha256": initial_esp_sha,
                "nvvars": (
                    {"path": str(esp / "NvVars"), "sha256": sha256(esp / "NvVars")}
                    if (esp / "NvVars").is_file() else None
                ),
                "first_boot": first,
                "second_boot": second,
                "active_slot_unchanged": bool(
                    after_recovery["active_slot_unchanged"]
                    and after_reopen["active_slot_unchanged"]
                ),
                "pending_generation": after_reopen["journal_generation"],
                "installed_component_count": len(
                    after_reopen["installed_components"]
                ),
                "serial_recovery": {
                    "path": str(first_log), "sha256": sha256(first_log)
                },
                "serial_reopen": {
                    "path": str(second_log), "sha256": sha256(second_log)
                },
            })
        report = {
            "schema": "ribon-qemu-update-power-cut-result-v1",
            "source_revision": args.source_revision,
            "qemu_version": qemu_tool.qemu_version(args.qemu),
            "network_enabled": False,
            "host_fault_event_count": coverage["event_count"],
            "selected_case_count": len(case_reports),
            "forced_kill_count": total_forced_kills,
            "process_group_cleanup_complete": all(
                case["first_boot"]["cleanup_complete"]
                and case["second_boot"]["cleanup_complete"]
                for case in case_reports
            ),
            "confirmed_predecessor_loss_count": sum(
                not case["active_slot_unchanged"] for case in case_reports
            ),
            "cases": case_reports,
            "artifacts": {
                "firmware": {"path": str(args.firmware), "sha256": sha256(args.firmware)},
                "boot_app": {"path": str(args.boot_app), "sha256": sha256(args.boot_app)},
                "manifest": {"path": str(args.manifest), "sha256": sha256(args.manifest)},
                "bundle": {"path": str(args.bundle), "sha256": sha256(args.bundle)},
                "product_manifest": {
                    "path": str(args.product_manifest),
                    "sha256": sha256(args.product_manifest),
                },
                "fixture_provenance": {
                    "path": str(args.fixture_provenance),
                    "sha256": sha256(args.fixture_provenance),
                },
                "fault_coverage": {
                    "path": str(args.coverage), "sha256": sha256(args.coverage)
                },
            },
        }
        output = args.results / "qemu-update-power-cut.json"
        output.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        print("RIBON-D04-QEMU-POWER-CUT-RECOVERY-OK cases=3 reboots=6")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"qemu-update-power-cut: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
