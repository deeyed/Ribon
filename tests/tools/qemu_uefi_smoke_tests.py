#!/usr/bin/env python3
"""Fixture tests for qemu_uefi_smoke.py diagnostics without launching QEMU."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import pathlib
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
SMOKE = ROOT / "tools" / "qemu_uefi_smoke.py"


def load_smoke_module():
    spec = importlib.util.spec_from_file_location("qemu_uefi_smoke", SMOKE)
    if spec is None or spec.loader is None:
        raise RuntimeError("failed to load qemu_uefi_smoke.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def test_forced_fallback_success(smoke) -> None:
    output = "\n".join(
        [
            "RIBON-UEFI-KERNEL-LOAD-START",
            "RIBON-UEFI-KERNEL-FALLBACK-ALLOC",
            "RIBON-UEFI-DIAG-STAGE=exit-boot-services",
            "PARUS-KCONSOLE-OK",
        ]
    )
    required = [
        "RIBON-UEFI-KERNEL-FALLBACK-ALLOC",
        "RIBON-UEFI-DIAG-STAGE=exit-boot-services",
        "PARUS-KCONSOLE-OK",
    ]
    expect(smoke.missing_required_markers(output, required) == [], "fallback fixture missing marker")
    expect(smoke.observed_failure_markers(output, smoke.FAILURE_MARKERS) == [], "fallback fixture saw failure")


def test_forced_diagnostic_failure(smoke) -> None:
    output = "\n".join(
        [
            "RIBON-UEFI-DIAG-STAGE=rph1-rebuild",
            "RIBON-UEFI-PLAN-FAIL",
        ]
    )
    failures = smoke.observed_failure_markers(output, smoke.FAILURE_MARKERS)
    expect(failures == ["RIBON-UEFI-PLAN-FAIL"], "diagnostic fixture did not catch plan failure")

    with tempfile.TemporaryDirectory(prefix="ribon-uefi-smoke-diag-") as tmp_name:
        log = pathlib.Path(tmp_name) / "serial.log"
        log.write_text(output, encoding="utf-8")
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            smoke.print_diagnostic("failure-marker", output, log, detail=", ".join(failures))
        diagnostic = stderr.getvalue()
    expect("RIBON-UEFI-SMOKE-DIAG=failure-marker" in diagnostic, "missing diagnostic marker")
    expect("RIBON-UEFI-SMOKE-LOG-TAIL-BEGIN" in diagnostic, "missing diagnostic log tail")
    expect("RIBON-UEFI-PLAN-FAIL" in diagnostic, "missing failure tail content")


def test_target_before_later_failure_is_success_boundary(smoke) -> None:
    output = "\n".join(
        [
            "RIBON-UEFI-EXIT-BOOT-SERVICES-START",
            "PARUS-HIGHER-HALF-CONTRACT-OK",
            "STORAGE-EXECUTOR-FAIL: verifier",
        ]
    )
    target_index = output.find("PARUS-HIGHER-HALF-CONTRACT-OK")
    failures_before_target = smoke.observed_failure_markers(
        output,
        smoke.FAILURE_MARKERS,
        before=target_index,
    )
    expect(failures_before_target == [], "later failure crossed the target boundary")
    first_failure = smoke.first_marker_position(output, smoke.FAILURE_MARKERS)
    expect(first_failure is not None, "fixture should still contain a later failure")
    expect(first_failure[0] > target_index, "failure marker should be after target marker")


def test_specific_failure_marker_deduplicates_generic_suffix(smoke) -> None:
    output = "STORAGE-EXECUTOR-FAIL: verifier"
    failures = smoke.observed_failure_markers(output, smoke.FAILURE_MARKERS)
    expect(
        failures == ["STORAGE-EXECUTOR-FAIL:"],
        "specific storage executor failure should not also report generic executor failure",
    )


def test_x86_64_qemu_command_shape(smoke) -> None:
    command = smoke.build_qemu_command(
        arch="x86_64",
        qemu="qemu-system-x86_64",
        firmware=pathlib.Path("/fw/OVMF_CODE.fd"),
        esp=pathlib.Path("/esp"),
        memory="256M",
    )
    expect("-machine" in command and "q35" in command, "x86_64 smoke should use q35")
    expect("-vga" in command and "std" in command, "x86_64 smoke should expose a VGA GOP candidate")
    expect(
        "format=raw,file=fat:rw:/esp" in command,
        "x86_64 smoke should attach the ESP as a FAT drive",
    )


def test_aarch64_qemu_command_shape(smoke) -> None:
    command = smoke.build_qemu_command(
        arch="aarch64",
        qemu="qemu-system-aarch64",
        firmware=pathlib.Path("/fw/AAVMF_CODE.fd"),
        vars_path=pathlib.Path("/fw/AAVMF_VARS.fd"),
        esp=pathlib.Path("/esp"),
        memory="512M",
    )
    expect("-machine" in command and "virt,gic-version=2" in command, "AArch64 smoke should use virt")
    expect("-cpu" in command and "cortex-a72" in command, "AArch64 smoke should pin the CPU model")
    expect(
        "if=pflash,format=raw,file=/fw/AAVMF_VARS.fd" in command,
        "AArch64 smoke should attach writable AAVMF vars pflash",
    )
    expect(
        "file=fat:rw:/esp,format=raw,if=none,id=hd0" in command,
        "AArch64 smoke should attach the ESP behind a virtio block device",
    )
    expect(
        "virtio-blk-device,drive=hd0,bootindex=0" in command,
        "AArch64 smoke should use the QEMU virt block device",
    )


def test_aarch64_storage_candidate_shape(smoke) -> None:
    command = smoke.build_qemu_command(
        arch="aarch64",
        qemu="qemu-system-aarch64",
        firmware=pathlib.Path("/fw/AAVMF_CODE.fd"),
        vars_path=pathlib.Path("/fw/AAVMF_VARS.fd"),
        esp=pathlib.Path("/esp"),
        memory="512M",
        storage_media="null-co",
        storage_size=1048576,
    )
    expect(
        "driver=null-co,node-name=storage0,size=1048576,read-zeroes=on" in command,
        "AArch64 runtime smoke should attach a null-co storage candidate",
    )
    expect(
        "virtio-blk-pci,drive=storage0,disable-legacy=on,bootindex=9" in command,
        "AArch64 runtime smoke should expose the storage candidate as modern virtio PCI",
    )


def main() -> int:
    smoke = load_smoke_module()
    test_forced_fallback_success(smoke)
    test_forced_diagnostic_failure(smoke)
    test_target_before_later_failure_is_success_boundary(smoke)
    test_specific_failure_marker_deduplicates_generic_suffix(smoke)
    test_x86_64_qemu_command_shape(smoke)
    test_aarch64_qemu_command_shape(smoke)
    test_aarch64_storage_candidate_shape(smoke)
    print("RIBON-UEFI-SMOKE-DIAGNOSTIC-TEST-OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
