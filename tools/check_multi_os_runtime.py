#!/usr/bin/env python3
"""Close a typed multi-OS QEMU evidence matrix from immutable result records."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


EXPECTED_ROWS = {
    "linux-aarch64-raw-fdt": {
        "payload_class": "linux-image",
        "target": "aarch64-virt-raw-fdt",
        "terminal": "clean-poweroff",
        "evidence_class": "qemu-runtime",
        "claim": "Linux AArch64 PID 1 and clean poweroff",
    },
    "linux-x86_64-uefi": {
        "payload_class": "linux-efi",
        "target": "x86_64-uefi-managed",
        "terminal": "clean-poweroff",
        "evidence_class": "qemu-runtime",
        "claim": "Linux x86_64 EFI stub PID 1 and clean poweroff",
    },
    "freebsd-amd64-uefi": {
        "payload_class": "freebsd-efi",
        "target": "x86_64-uefi-freebsd",
        "terminal": "required-evidence-observed",
        "evidence_class": "qemu-runtime",
        "claim": "FreeBSD amd64 loader and kernel single-user terminal",
    },
    "linux-riscv64-opensbi": {
        "payload_class": "linux-riscv64-image",
        "target": "riscv64-virt-opensbi",
        "terminal": "clean-poweroff",
        "evidence_class": "qemu-runtime",
        "claim": "Linux RISC-V64 PID 1 and clean poweroff",
    },
    "parus-aarch64-rph1-fixture": {
        "payload_class": "fixture",
        "target": "aarch64-virt-raw-fdt",
        "terminal": "required-evidence-observed",
        "evidence_class": "qemu-contract-fixture",
        "claim": "AArch64 Parus protocol entry regression fixture",
    },
    "parus-x86_64-rph1-fixture": {
        "payload_class": "fixture",
        "target": "x86_64-uefi",
        "terminal": "required-evidence-observed",
        "evidence_class": "qemu-contract-fixture",
        "claim": "x86_64 Parus protocol entry regression fixture",
    },
    "parus-riscv64-rph1-fixture": {
        "payload_class": "fixture",
        "target": "riscv64-virt-opensbi",
        "terminal": "required-evidence-observed",
        "evidence_class": "qemu-contract-fixture",
        "claim": "RISC-V64 RPH1 and bootstrap-hart regression fixture",
    },
}


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_result(
    label: str,
    path: Path,
    source_revision: str,
) -> dict[str, object]:
    """Validate one supervised result without weakening tuple-specific claims."""

    expected = EXPECTED_ROWS[label]
    report = json.loads(path.read_text(encoding="utf-8"))
    cleanup = report.get("cleanup") if isinstance(report, dict) else None
    raw_serial = report.get("raw_serial") if isinstance(report, dict) else None
    payload = report.get("payload") if isinstance(report, dict) else None
    composed = report.get("composed_artifact") if isinstance(report, dict) else None
    product_manifest = report.get("product_manifest") if isinstance(report, dict) else None
    firmware = report.get("firmware") if isinstance(report, dict) else None
    external_validation = (
        report.get("external_payload_validation") if isinstance(report, dict) else None
    )
    qemu = report.get("qemu") if isinstance(report, dict) else None
    if (
        not isinstance(report, dict)
        or report.get("schema") != "ribon-qemu-payload-evidence-v1"
        or report.get("outcome") != "passed"
        or report.get("first_divergence") is not None
        or report.get("source_revision") != source_revision
        or report.get("target") != expected["target"]
        or report.get("observed_payload_class") != expected["payload_class"]
        or report.get("terminal") != expected["terminal"]
        or not isinstance(cleanup, dict)
        or cleanup.get("complete") is not True
        or cleanup.get("forced_kill") is not False
        or cleanup.get("process_group_alive_after_cleanup") is not False
        or not isinstance(raw_serial, dict)
        or raw_serial.get("preserved") is not True
        or not isinstance(raw_serial.get("sha256"), str)
        or not isinstance(payload, dict)
        or payload.get("immutable") is not True
        or payload.get("sha256") != payload.get("sha256_after_run")
        or not isinstance(composed, dict)
        or composed.get("immutable") is not True
        or composed.get("sha256") != composed.get("sha256_after_run")
        or (product_manifest is not None and not isinstance(product_manifest, dict))
        or (firmware is not None and not isinstance(firmware, dict))
        or (external_validation is not None and not isinstance(external_validation, dict))
        or not isinstance(qemu, dict)
    ):
        raise ValueError(f"{label}: result contract mismatch")
    return {
        "claim": expected["claim"],
        "claim_scope": "exact named target, payload, and supervised QEMU tuple",
        "composed_artifact_sha256": composed["sha256"],
        "evidence_class": expected["evidence_class"],
        "external_validation_sha256": (
            external_validation.get("sha256")
            if isinstance(external_validation, dict)
            else None
        ),
        "firmware_sha256": (
            firmware.get("sha256") if isinstance(firmware, dict) else None
        ),
        "non_claim": (
            "Does not establish current Parus kernel runtime boot"
            if expected["evidence_class"] == "qemu-contract-fixture"
            else "Does not establish physical-hardware or production-firmware support"
        ),
        "payload_sha256": payload["sha256"],
        "product_id": (
            product_manifest.get("product_id")
            if isinstance(product_manifest, dict)
            else None
        ),
        "product_manifest_sha256": (
            product_manifest.get("sha256")
            if isinstance(product_manifest, dict)
            else None
        ),
        "qemu_version": qemu.get("version"),
        "raw_serial_sha256": raw_serial["sha256"],
        "result_path": str(path),
        "result_sha256": sha256_file(path),
        "target": expected["target"],
        "terminal": expected["terminal"],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--output", type=Path, required=True)
    for label in EXPECTED_ROWS:
        parser.add_argument(f"--{label}", type=Path, required=True)
    args = parser.parse_args()

    rows = {
        label: validate_result(
            label,
            getattr(args, label.replace("-", "_")),
            args.source_revision,
        )
        for label in EXPECTED_ROWS
    }
    matrix = {
        "schema": "ribon-multi-os-runtime-matrix-v1",
        "source_revision": args.source_revision,
        "rows": rows,
        "summary": {
            "qemu_contract_fixture_rows": sum(
                row["evidence_class"] == "qemu-contract-fixture"
                for row in rows.values()
            ),
            "qemu_runtime_rows": sum(
                row["evidence_class"] == "qemu-runtime"
                for row in rows.values()
            ),
            "physical_hardware": "not-run",
            "production_firmware": "not-claimed",
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(matrix, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print("RIBON-R04-MULTI-OS-RUNTIME-OK runtime=4 regression=3 hardware=not-run")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
