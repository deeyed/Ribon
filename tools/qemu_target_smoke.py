#!/usr/bin/env python3
"""Run a bounded Ribon target smoke and preserve payload-aware evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
from pathlib import PurePosixPath
import signal
import subprocess
import time


TARGET_MARKERS = {
    "aarch64-virt-raw-fdt": (
        b"RIBON-R4-RAW-FDT-ENTRY",
        b"RIBON-R4-FDT-ACCEPTED",
        b"RIBON-R4-PRODUCT-GRAPH-OK",
        b"RIBON-R4-PROTOCOL-HANDOFF-OK",
        b"RIBON-R4-PAYLOAD-LOADED",
        b"RIBON-R4-RAW-FDT-TRANSFER",
    ),
    "riscv64-virt-opensbi": (
        b"RIBON-R4-RAW-FDT-ENTRY",
        b"RIBON-R4-FDT-ACCEPTED",
        b"RIBON-R4-PRODUCT-GRAPH-OK",
        b"RIBON-R4-PROTOCOL-HANDOFF-OK",
        b"RIBON-R4-PAYLOAD-LOADED",
        b"RIBON-R4-RAW-FDT-TRANSFER",
    ),
    "x86_64-uefi": (
        b"RIBON-R4-UEFI-ENTRY",
        b"RIBON-R8-UEFI-CONFIG-OK",
        b"RIBON-R9-UEFI-MODULE-LOADED",
        b"RIBON-R4-UEFI-MEMORY-MAP",
        b"RIBON-R4-UEFI-PRODUCT-GRAPH-OK",
        b"RIBON-R4-PROTOCOL-HANDOFF-OK",
        b"RIBON-R4-UEFI-PAYLOAD-LOADED",
        b"RIBON-R8-UEFI-ESP-PAYLOAD-OK",
        b"RIBON-R4-UEFI-FINAL-HANDOFF-OK",
        b"RIBON-R4-UEFI-EXIT-BOOT-SERVICES-OK",
        b"RIBON-R4-UEFI-TRANSFER",
    ),
    "x86_64-uefi-managed": (
        b"RIBON-R4-UEFI-ENTRY",
        b"RIBON-R8-UEFI-CONFIG-OK",
        b"RIBON-R4-UEFI-MEMORY-MAP",
        b"RIBON-R4-UEFI-PRODUCT-GRAPH-OK",
        b"RIBON-R02-UEFI-MANAGED-IMAGE-VALIDATED",
        b"RIBON-R02-UEFI-MANAGED-LAUNCH-ATTEMPT",
    ),
}
LEGACY_FIXTURE_MARKERS = (
    b"PARUS-FIXTURE-ENTRY-OK",
    b"PARUS-FIXTURE-ENTRY-ABI-FAIL",
)
TARGET_FIXTURE_SUCCESS_MARKERS = {
    "aarch64-virt-raw-fdt": b"PARUS-FIXTURE-ENTRY-OK",
    "riscv64-virt-opensbi": b"RIBON-RPH1-RISCV64-FIXTURE-OK",
    "x86_64-uefi": b"PARUS-FIXTURE-ENTRY-OK",
    "x86_64-uefi-managed": b"PARUS-FIXTURE-ENTRY-OK",
}
FIXTURE_FAILURE_MARKERS = (
    b"PARUS-FIXTURE-ENTRY-ABI-FAIL",
    b"RIBON-RPH1-RISCV64-FIXTURE-FAIL:",
)
FIXTURE_PROVENANCE = (
    b"RIBON-FIXTURE-PAYLOAD-V1",
    b"RIBON-RISCV64-RPH1-FIXTURE-V1",
)
FATAL_OUTPUT_MARKERS = (
    b"PANIC",
    b"Kernel panic",
    b"Unhandled exception",
    b"qemu: fatal",
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


def load_module_provenance(path: Path) -> dict[str, object]:
    """Validate one generated bundle report and all immutable snapshots."""

    try:
        report = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot load module provenance: {error}") from error
    expected_keys = {
        "bundle_sha256",
        "component_count",
        "components",
        "product_id",
        "product_manifest_sha256",
        "schema",
    }
    components = report.get("components") if isinstance(report, dict) else None
    if (
        not isinstance(report, dict)
        or set(report) != expected_keys
        or report.get("schema") != "ribon-boot-module-bundle-provenance-v1"
        or not isinstance(report.get("component_count"), int)
        or not 1 <= report["component_count"] <= 8
        or not isinstance(components, list)
        or len(components) != report["component_count"]
        or not isinstance(report.get("product_id"), str)
        or not report["product_id"]
        or not isinstance(report.get("product_manifest_sha256"), str)
        or len(report["product_manifest_sha256"]) != 64
    ):
        raise ValueError("module provenance has an invalid v1 envelope")
    product_root = path.parent.parent.resolve()
    bundle_digest = hashlib.sha256()
    bundle_digest.update(report["schema"].encode("ascii") + b"\0")
    names: set[str] = set()
    initial_images = 0
    component_keys = {
        "index",
        "maximum_size",
        "name",
        "role",
        "sha256",
        "size",
        "snapshot",
        "source",
    }
    for index, component in enumerate(components):
        if not isinstance(component, dict) or set(component) != component_keys:
            raise ValueError("module provenance component shape is invalid")
        name = component.get("name")
        role = component.get("role")
        size = component.get("size")
        maximum_size = component.get("maximum_size")
        digest = component.get("sha256")
        if (
            component.get("index") != index
            or not isinstance(name, str)
            or not 1 <= len(name) <= 63
            or not name.isascii()
            or any(not (ch.isalnum() or ch in "._-") for ch in name)
            or name in names
            or role not in ("initial-image", "auxiliary")
            or not isinstance(size, int)
            or isinstance(size, bool)
            or size <= 0
            or not isinstance(maximum_size, int)
            or isinstance(maximum_size, bool)
            or maximum_size < size
            or not isinstance(digest, str)
            or len(digest) != 64
            or any(ch not in "0123456789abcdef" for ch in digest)
        ):
            raise ValueError("module provenance component value is invalid")
        logical_snapshot = PurePosixPath(str(component.get("snapshot")))
        logical_source = PurePosixPath(str(component.get("source")))
        if (
            logical_snapshot.is_absolute()
            or logical_source.is_absolute()
            or any(part in ("", ".", "..") for part in logical_snapshot.parts)
            or any(part in ("", ".", "..") for part in logical_source.parts)
        ):
            raise ValueError("module provenance path is not bounded")
        snapshot = product_root.joinpath(*logical_snapshot.parts).resolve()
        try:
            snapshot.relative_to(product_root)
        except ValueError as error:
            raise ValueError("module snapshot escapes the product root") from error
        if (
            not snapshot.is_file()
            or snapshot.stat().st_size != size
            or sha256_file(snapshot) != digest
        ):
            raise ValueError("module snapshot identity is invalid")
        names.add(name)
        if role == "initial-image":
            initial_images += 1
            if initial_images > 1:
                raise ValueError("module provenance has duplicate initial images")
        data = snapshot.read_bytes()
        bundle_digest.update(name.encode("ascii") + b"\0")
        bundle_digest.update(role.encode("ascii") + b"\0")
        bundle_digest.update(len(data).to_bytes(8, "little"))
        bundle_digest.update(data)
    if report.get("bundle_sha256") != bundle_digest.hexdigest():
        raise ValueError("module bundle digest is invalid")
    return report


def module_product_is_bound(
    product: object,
    product_hash: str | None,
    provenance: dict[str, object],
    target: str,
) -> bool:
    """Return whether one raw-FDT product exactly authorizes module publication."""

    services = product.get("services") if isinstance(product, dict) else None
    module_services = (
        [
            service
            for service in services
            if isinstance(service, dict)
            and service.get("kind") == "boot-module-bundle"
        ]
        if isinstance(services, list)
        else []
    )
    exact_service = {
        "id": "service.product.boot-module-bundle",
        "kind": "boot-module-bundle",
        "symbol": "ribon_generated_boot_module_bundle_service_descriptor",
    }
    expected_target = {
        "aarch64-virt-raw-fdt": (
            "qemu-aarch64-virt-raw-fdt",
            "aarch64",
            "qemu-virt-aarch64",
        ),
        "riscv64-virt-opensbi": (
            "qemu-riscv64-virt-opensbi",
            "riscv64",
            "qemu-virt-riscv64",
        ),
    }.get(target)
    return (
        isinstance(product, dict)
        and expected_target is not None
        and product.get("schema_version") == 1
        and product.get("product_kind") == "bootloader"
        and product.get("target_id") == expected_target[0]
        and product.get("architecture") == expected_target[1]
        and product.get("environment") == "raw-fdt"
        and product.get("port") == expected_target[2]
        and product.get("boot_module_bundle") == {
            "component_manifest_schema": "ribon-boot-module-components-v1",
            "maximum_modules": 8,
            "provider": "generated-component-bundle-v1",
        }
        and module_services == [exact_service]
        and "BOOT_MODULE_BUNDLE" in product.get("required_capabilities", [])
        and "BOOT_MODULE_BUNDLE" in product.get("allowed_capabilities", [])
        and provenance.get("product_id") == product.get("product_id")
        and provenance.get("product_manifest_sha256") == product_hash
    )


def module_image_binding(
    image_path: Path,
    provenance_path: Path,
    provenance: dict[str, object],
) -> list[dict[str, object]]:
    """Bind ordinal snapshots to the canonical page-backed raw-image suffix."""

    image = image_path.read_bytes()
    components = provenance["components"]
    assert isinstance(components, list)
    backing_sizes = [
        (int(component["size"]) + 4095) & ~4095
        for component in components
        if isinstance(component, dict)
    ]
    if len(backing_sizes) != len(components):
        raise ValueError("module component table is invalid")
    offset = len(image) - sum(backing_sizes)
    if offset < 0 or offset % 4096 != 0:
        raise ValueError("module section is not a page-aligned image suffix")
    product_root = provenance_path.parent.parent.resolve()
    bindings: list[dict[str, object]] = []
    for index, (component, backing_size) in enumerate(
        zip(components, backing_sizes)
    ):
        assert isinstance(component, dict)
        logical_snapshot = PurePosixPath(str(component["snapshot"]))
        snapshot = product_root.joinpath(*logical_snapshot.parts)
        data = snapshot.read_bytes()
        if image[offset : offset + len(data)] != data:
            raise ValueError("module snapshot is not bound to the composed image")
        bindings.append(
            {
                "backing_size": backing_size,
                "image_offset": offset,
                "index": index,
                "sha256": component["sha256"],
                "size": len(data),
            }
        )
        offset += backing_size
    if offset != len(image):
        raise ValueError("module backing does not close at the image end")
    return bindings


def observed_payload_class(path: Path) -> str:
    """Distinguish generated fixtures, external ELF, and Linux raw Image."""
    prefix = path.read_bytes()
    if len(prefix) >= 64 and prefix[56:60] == b"ARMd":
        return "linux-image"
    if prefix.startswith(b"MZ"):
        return "linux-efi"
    if not prefix.startswith(b"\x7fELF"):
        return "invalid"
    if (
        any(provenance in prefix for provenance in FIXTURE_PROVENANCE)
        or any(marker in prefix for marker in LEGACY_FIXTURE_MARKERS)
        or any(
            marker in prefix
            for marker in TARGET_FIXTURE_SUCCESS_MARKERS.values()
        )
    ):
        return "fixture"
    return "kernel"


def command_for(args: argparse.Namespace) -> list[str]:
    """Build the selected QEMU command without launching it."""
    if args.target == "aarch64-virt-raw-fdt":
        if args.image is None:
            raise ValueError("--image is required")
        command = [
            args.qemu,
            "-machine", "virt",
            "-cpu", "cortex-a72",
            "-m", "256M",
            "-nographic",
            "-monitor", "none",
            "-net", "none",
            "-no-reboot",
            "-kernel", str(args.image),
        ]
        if not args.expect_clean_exit:
            command[command.index("-kernel"):command.index("-kernel")] = [
                "-no-shutdown"
            ]
        if args.preload_payload_address is not None:
            command += [
                "-device",
                "loader,file=" + str(args.payload) +
                f",addr={args.preload_payload_address},force-raw=on",
            ]
        if args.kernel_command_line is not None:
            command += ["-append", args.kernel_command_line]
        return command
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
    # Keep firmware NvVars writes in a transient overlay over the immutable ESP.
    command = [
        args.qemu,
        "-machine", "q35",
        "-m", "256M",
        "-display", "none",
        "-serial", "stdio",
        "-monitor", "none",
        "-net", "none",
        "-no-reboot",
        "-snapshot",
        "-drive", f"if=pflash,format=raw,readonly=on,file={args.firmware}",
        "-drive", f"format=raw,file=fat:{args.esp}",
    ]
    if not args.expect_clean_exit:
        command.insert(command.index("-snapshot"), "-no-shutdown")
    return command


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
    candidates = TARGET_MARKERS[args.target]
    candidates += tuple(
        marker.encode("utf-8") for marker in args.required_marker
    )
    if args.expected_payload_class == "fixture":
        candidates += (TARGET_FIXTURE_SUCCESS_MARKERS[args.target],)
    markers: list[bytes] = []
    seen: set[bytes] = set()
    for marker in candidates:
        if marker not in seen:
            markers.append(marker)
            seen.add(marker)
    return tuple(markers)


def required_markers_anywhere(args: argparse.Namespace) -> tuple[bytes, ...]:
    """Return unique receipts whose placement is outside Ribon's stage order."""

    markers: list[bytes] = []
    seen: set[bytes] = set()
    for value in args.required_marker_anywhere:
        marker = value.encode("utf-8")
        if marker not in seen:
            markers.append(marker)
            seen.add(marker)
    return tuple(markers)


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
    parser.add_argument("--init-image", type=Path)
    parser.add_argument("--product-manifest", type=Path)
    parser.add_argument("--module-provenance", type=Path)
    parser.add_argument("--external-payload-validation", type=Path)
    parser.add_argument(
        "--expected-payload-class",
        choices=("fixture", "kernel", "linux-image", "linux-efi"),
        required=True,
    )
    parser.add_argument("--expected-payload-sha256")
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--required-marker", action="append", default=[])
    parser.add_argument(
        "--required-marker-anywhere", action="append", default=[]
    )
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--preload-payload-address", type=lambda value: int(value, 0))
    parser.add_argument("--kernel-command-line")
    parser.add_argument("--expect-clean-exit", action="store_true")
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--result", type=Path, required=True)
    args = parser.parse_args()

    command = command_for(args)
    composed_path = args.image if args.image is not None else args.esp
    assert composed_path is not None
    payload_hash = artifact_sha256(args.payload)
    init_image_hash = (
        artifact_sha256(args.init_image)
        if args.init_image is not None
        else None
    )
    module_provenance_hash = (
        artifact_sha256(args.module_provenance)
        if args.module_provenance is not None
        else None
    )
    external_validation_hash = (
        artifact_sha256(args.external_payload_validation)
        if args.external_payload_validation is not None
        else None
    )
    external_validation_report = None
    module_report = None
    module_image_bindings = None
    product_report = None
    product_manifest_hash = (
        artifact_sha256(args.product_manifest)
        if args.product_manifest is not None
        else None
    )
    composed_hash = artifact_sha256(composed_path)
    payload_class = observed_payload_class(args.payload)
    markers = required_markers(args)
    anywhere_markers = required_markers_anywhere(args)
    started = time.monotonic()
    output = bytearray()
    outcome = "preflight-failure"
    terminal = "not-launched"
    timed_out = False
    forced_kill = False
    launched = False
    cleanup_complete = True
    process_group_alive_after_cleanup = False
    process_returncode = None

    preflight_error = None
    if args.expected_payload_sha256 not in (None, payload_hash):
        preflight_error = "payload-hash-mismatch"
    elif payload_class != args.expected_payload_class:
        preflight_error = "payload-class-mismatch"
    elif args.expected_payload_class == "linux-image" and (
        args.external_payload_validation is None
        or args.preload_payload_address is None
        or not args.expect_clean_exit
    ):
        preflight_error = "linux-runtime-contract-incomplete"
    elif args.expected_payload_class == "linux-efi" and (
        args.external_payload_validation is None
        or not args.expect_clean_exit
        or args.init_image is None
    ):
        preflight_error = "linux-efi-runtime-contract-incomplete"
    elif args.module_provenance is not None and args.product_manifest is None:
        preflight_error = "module-product-manifest-required"
    elif args.product_manifest is not None:
        try:
            product_report = json.loads(
                args.product_manifest.read_text(encoding="utf-8")
            )
        except (OSError, UnicodeError, json.JSONDecodeError):
            preflight_error = "product-manifest-invalid"
    if preflight_error is None and args.external_payload_validation is not None:
        try:
            external_validation_report = json.loads(
                args.external_payload_validation.read_text(encoding="utf-8")
            )
            artifact = external_validation_report.get("artifact")
            placement = external_validation_report.get("placement")
            raw_linux = (
                isinstance(artifact, dict)
                and
                external_validation_report.get("schema") ==
                    "ribon-external-linux-image-validation-v1"
                and artifact.get("class") == "linux-aarch64-image"
                and isinstance(placement, dict)
                and placement.get("base") == args.preload_payload_address
            )
            efi_linux = (
                isinstance(artifact, dict)
                and
                external_validation_report.get("schema") ==
                    "ribon-external-linux-efi-validation-v1"
                and artifact.get("class") == "linux-x86_64-efi-stub"
                and external_validation_report.get("pe", {}).get("machine") == 0x8664
                and external_validation_report.get("pe", {}).get("subsystem") == 10
            )
            if (
                not isinstance(artifact, dict)
                or artifact.get("sha256") != payload_hash
                or artifact.get("size") != args.payload.stat().st_size
                or not (raw_linux or efi_linux)
            ):
                raise ValueError("external validation does not bind payload")
        except (OSError, UnicodeError, json.JSONDecodeError, ValueError):
            preflight_error = "external-payload-validation-invalid"
    if (
        preflight_error is None
        and args.module_provenance is None
        and isinstance(product_report, dict)
        and "boot_module_bundle" in product_report
    ):
        preflight_error = "module-provenance-required"
    if preflight_error is None and args.module_provenance is not None:
        try:
            module_report = load_module_provenance(args.module_provenance)
        except ValueError:
            preflight_error = "module-provenance-invalid"
        if (
            preflight_error is None
            and not module_product_is_bound(
                product_report,
                product_manifest_hash,
                module_report,
                args.target,
            )
        ):
            preflight_error = "module-product-mismatch"
        if preflight_error is None:
            try:
                if args.image is None:
                    raise ValueError("module evidence requires a raw image")
                module_image_bindings = module_image_binding(
                    args.image,
                    args.module_provenance,
                    module_report,
                )
            except (OSError, ValueError):
                preflight_error = "module-image-mismatch"

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
                    target_failed = any(
                        line.startswith(b"RIBON-R4-") and b"-FAIL" in line
                        for line in output.splitlines()
                    )
                    if target_failed:
                        outcome = "target-failure"
                        terminal = "target-failure"
                        break
                    payload_failed = any(
                        (
                            line.startswith(b"PARUS:BM:")
                            and b":FAIL:" in line
                        )
                        or line.startswith(b"PARUS:EXC:")
                        for line in output.splitlines()
                    ) or any(marker in output for marker in FATAL_OUTPUT_MARKERS)
                    if payload_failed:
                        outcome = "payload-failure"
                        terminal = "payload-failure"
                        break
                    if any(
                        marker in output
                        for marker in FIXTURE_FAILURE_MARKERS
                    ):
                        outcome = "payload-abi-failure"
                        terminal = "payload-abi-failure"
                        break
                    observations, divergence = marker_observations(
                        bytes(output),
                        markers,
                    )
                    anywhere_observations, _ = marker_observations(
                        bytes(output), anywhere_markers
                    )
                    if (
                        divergence is None
                        and all(item["count"] == 1 for item in observations)
                        and all(
                            item["count"] == 1
                            for item in anywhere_observations
                        )
                    ) and not args.expect_clean_exit:
                        outcome = "passed"
                        terminal = "required-evidence-observed"
                        break
                if process.poll() is not None:
                    outcome = (
                        "clean-exit-candidate"
                        if args.expect_clean_exit else "early-exit"
                    )
                    terminal = "process-exit"
                    break
                time.sleep(0.02)
            else:
                timed_out = True
                terminal = "timeout"
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
            process_group_alive_after_cleanup = process_group_alive(process.pid)
            process_returncode = process.returncode
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
    anywhere_observations, _ = marker_observations(
        bytes(output), anywhere_markers
    )
    target_failed = any(
        line.startswith(b"RIBON-R4-") and b"-FAIL" in line
        for line in output.splitlines()
    )
    payload_failed = any(
        (line.startswith(b"PARUS:BM:") and b":FAIL:" in line)
        or line.startswith(b"PARUS:EXC:")
        for line in output.splitlines()
    ) or any(marker in output for marker in FATAL_OUTPUT_MARKERS)
    fixture_failed = any(marker in output for marker in FIXTURE_FAILURE_MARKERS)
    if (
        outcome == "clean-exit-candidate"
        and process_returncode == 0
        and first_divergence is None
        and not target_failed
        and not payload_failed
        and not fixture_failed
    ):
        outcome = "passed"
        terminal = "clean-poweroff"
    if outcome == "passed" and target_failed:
        outcome = "target-failure"
        terminal = "target-failure"
        first_divergence = "target-failure-after-required-evidence"
    elif outcome == "passed" and payload_failed:
        outcome = "payload-failure"
        terminal = "payload-failure"
        first_divergence = "payload-failure-after-required-evidence"
    elif outcome == "passed" and fixture_failed:
        outcome = "payload-abi-failure"
        terminal = "payload-abi-failure"
        first_divergence = "payload-abi-failure-after-required-evidence"
    if first_divergence is None:
        for item in anywhere_observations:
            if item["count"] == 0:
                first_divergence = f"missing-anywhere:{item['marker']}"
                break
            if item["count"] != 1:
                first_divergence = f"duplicate-anywhere:{item['marker']}"
                break
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
    module_provenance_hash_after = (
        artifact_sha256(args.module_provenance)
        if args.module_provenance is not None
        else None
    )
    module_snapshots_immutable = module_report is not None
    if launched and args.module_provenance is not None:
        try:
            module_snapshots_immutable = (
                load_module_provenance(args.module_provenance) == module_report
            )
        except ValueError:
            module_snapshots_immutable = False
    if launched and args.module_provenance is not None and (
        module_provenance_hash_after != module_provenance_hash
        or not module_snapshots_immutable
    ):
        outcome = "module-provenance-mutated"
        terminal = "artifact-identity-failure"
        first_divergence = "module-bundle-mutated-during-run"

    product_manifest_hash_after = (
        artifact_sha256(args.product_manifest)
        if args.product_manifest is not None
        else None
    )
    if launched and product_manifest_hash_after != product_manifest_hash:
        outcome = "product-manifest-mutated"
        terminal = "artifact-identity-failure"
        first_divergence = "product-manifest-mutated-during-run"

    composed_hash_after = artifact_sha256(composed_path)
    if launched and composed_hash_after != composed_hash:
        outcome = "composed-artifact-mutated"
        terminal = "artifact-identity-failure"
        first_divergence = "composed-artifact-mutated-during-run"

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
        "initial_image": (
            {
                "path": str(args.init_image),
                "sha256": init_image_hash,
            }
            if args.init_image is not None
            else None
        ),
        "product_manifest": (
            {
                "path": str(args.product_manifest),
                "sha256": product_manifest_hash,
                "sha256_after_run": product_manifest_hash_after,
                "immutable":
                    product_manifest_hash_after == product_manifest_hash,
                "product_id": (
                    product_report.get("product_id")
                    if isinstance(product_report, dict)
                    else None
                ),
            }
            if args.product_manifest is not None
            else None
        ),
        "boot_module_bundle": (
            {
                "path": str(args.module_provenance),
                "sha256": module_provenance_hash,
                "sha256_after_run": module_provenance_hash_after,
                "immutable": (
                    module_provenance_hash == module_provenance_hash_after
                    and module_snapshots_immutable
                ),
                "provenance": module_report,
                "image_binding": module_image_bindings,
            }
            if args.module_provenance is not None
            else None
        ),
        "external_payload_validation": (
            {
                "path": str(args.external_payload_validation),
                "sha256": external_validation_hash,
                "report": external_validation_report,
            }
            if args.external_payload_validation is not None
            else None
        ),
        "composed_artifact": {
            "path": str(composed_path),
            "sha256": composed_hash,
            "sha256_after_run": composed_hash_after,
            "immutable": composed_hash_after == composed_hash,
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
            "returncode": process_returncode,
            "expected_clean_exit": args.expect_clean_exit,
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
        "required_markers_anywhere": [
            marker.decode("utf-8") for marker in anywhere_markers
        ],
        "marker_observations_anywhere": anywhere_observations,
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
