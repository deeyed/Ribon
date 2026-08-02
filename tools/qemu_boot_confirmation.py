#!/usr/bin/env python3
"""Run the three-boot D06 pending-to-confirmed q35 UEFI scenario."""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys


def load_base():
    path = Path(__file__).with_name("qemu_update_install.py")
    spec = importlib.util.spec_from_file_location("ribon_d06_qemu_base", path)
    if spec is None or spec.loader is None:
        raise ValueError("cannot load qemu_update_install.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def inspect(args: argparse.Namespace, tool: Path, output: Path) -> dict[str, object]:
    command = [
        sys.executable, str(tool), "--disk", str(args.disk),
        "--manifest", str(args.manifest), "--expected-active-sha256",
        args.expected_active_sha256, "--output", str(output),
    ]
    completed = subprocess.run(
        command, check=False, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
    )
    if completed.returncode != 0:
        raise ValueError(completed.stdout.decode("utf-8", errors="replace"))
    return json.loads(output.read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--esp-template", type=Path, required=True)
    parser.add_argument("--disk-fixture", type=Path, required=True)
    parser.add_argument("--boot-app", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--confirmation", type=Path, required=True)
    parser.add_argument("--product-manifest", type=Path, required=True)
    parser.add_argument("--fixture-provenance", type=Path, required=True)
    parser.add_argument("--pending-inspector", type=Path, required=True)
    parser.add_argument("--confirmed-inspector", type=Path, required=True)
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=20.0)
    args = parser.parse_args()
    try:
        base = load_base()
        provenance = json.loads(args.fixture_provenance.read_text(encoding="utf-8"))
        args.expected_active_sha256 = provenance.get("active_slot_sha256")
        expected_disk = provenance.get("artifacts", {}).get(
            "update-disk.raw", {}
        ).get("sha256")
        expected_confirmation = provenance.get("artifacts", {}).get(
            "confirmation.bin", {}
        ).get("sha256")
        if (
            provenance.get("schema") != "ribon-qemu-update-fixture-v1"
            or not isinstance(args.expected_active_sha256, str)
            or not isinstance(expected_disk, str)
            or not isinstance(expected_confirmation, str)
            or base.sha256(args.disk_fixture) != expected_disk
            or base.sha256(args.confirmation) != expected_confirmation
        ):
            raise ValueError("fixture provenance identity mismatch")
        args.results.mkdir(parents=True, exist_ok=True)
        runtime_esp = args.results / "esp"
        if runtime_esp.exists():
            shutil.rmtree(runtime_esp)
        shutil.copytree(args.esp_template, runtime_esp)
        (runtime_esp / "RIBON" / "TRANSACT.V1").write_bytes(b"RIBON-D04-TXN-V1")
        (runtime_esp / "RIBON" / "CONFIRM.V1").write_bytes(b"RIBON-D06-CFM-V1")
        args.esp = runtime_esp
        args.disk = args.results / "boot-confirmation-runtime.raw"
        shutil.copyfile(args.disk_fixture, args.disk)
        initial_disk_sha = base.sha256(args.disk)
        initial_esp_sha = base.tree_digest(args.esp)
        markers = [
            b"RIBON-D06-PENDING-BOOT-ATTEMPT",
            b"RIBON-D06-CONFIRMATION-CONFIRMED",
            b"RIBON-D06-CONFIRMATION-REOPEN-CONFIRMED",
        ]
        receipts = []
        commands = []
        logs = []
        inspections = []
        for index, marker in enumerate(markers, start=1):
            pflash = args.results / f"qemu-pflash-{index}.fd"
            shutil.copyfile(args.firmware, pflash)
            command = base.command(args, pflash)
            output, receipt = base.boot_once(command, marker, args.timeout)
            log = args.results / f"qemu-boot-{index}.log"
            log.write_bytes(output)
            if (
                receipt["outcome"] != "passed"
                or receipt["forced_kill"]
                or not receipt["cleanup_complete"]
            ):
                raise ValueError(f"QEMU boot {index} failed: {receipt}")
            inspector = args.pending_inspector if index == 1 else \
                args.confirmed_inspector
            inspection_path = args.results / f"disk-after-boot-{index}.json"
            inspections.append(inspect(args, inspector, inspection_path))
            receipts.append(receipt)
            commands.append(command)
            logs.append(log)
        if base.tree_digest(args.esp) != initial_esp_sha:
            raise ValueError("immutable ESP inputs changed during confirmation boots")
        if inspections[0].get("target_state") != "PENDING":
            raise ValueError("first boot did not preserve exact pending transaction")
        for value in inspections[1:]:
            if value.get("update", {}).get("state") != "CONFIRMED":
                raise ValueError("confirmed journal did not reopen")
        artifacts = {
            "runtime_disk": {
                "path": str(args.disk),
                "sha256_before": initial_disk_sha,
                "sha256_after": base.sha256(args.disk),
            },
            "boot_app": {"path": str(args.boot_app),
                         "sha256": base.sha256(args.boot_app)},
            "firmware": {"path": str(args.firmware),
                         "sha256": base.sha256(args.firmware)},
            "manifest": {"path": str(args.manifest),
                         "sha256": base.sha256(args.manifest)},
            "confirmation": {"path": str(args.confirmation),
                             "sha256": base.sha256(args.confirmation)},
            "product_manifest": {"path": str(args.product_manifest),
                                 "sha256": base.sha256(args.product_manifest)},
            "fixture_provenance": {"path": str(args.fixture_provenance),
                                   "sha256": base.sha256(args.fixture_provenance)},
            "serial_logs": [
                {"path": str(path), "sha256": base.sha256(path)} for path in logs
            ],
        }
        report = {
            "schema": "ribon-qemu-boot-confirmation-result-v1",
            "source_revision": args.source_revision,
            "qemu_version": base.qemu_version(args.qemu),
            "commands": commands,
            "network_enabled": False,
            "boots": receipts,
            "first_boot_pending": True,
            "second_boot_confirmed": True,
            "third_boot_idempotent_reopen": True,
            "process_group_cleanup_complete": all(
                value["cleanup_complete"] for value in receipts
            ),
            "forced_kill_count": sum(bool(value["forced_kill"]) for value in receipts),
            "provider_class": "reference",
            "artifacts": artifacts,
        }
        result = args.results / "qemu-boot-confirmation.json"
        result.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        if not report["process_group_cleanup_complete"] or report["forced_kill_count"]:
            raise ValueError("QEMU cleanup closure is incomplete")
        print("RIBON-D06-QEMU-BOOT-CONFIRMATION-OK boots=3 forced-kill=0")
        return 0
    except (OSError, ValueError, subprocess.SubprocessError,
            json.JSONDecodeError) as error:
        print(f"qemu-boot-confirmation: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
