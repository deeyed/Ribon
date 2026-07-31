#!/usr/bin/env python3
"""Generate a deterministic embedded boot-module bundle from exact components."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import sys


MANIFEST_SCHEMA = "ribon-boot-module-components-v1"
PROVENANCE_SCHEMA = "ribon-boot-module-bundle-provenance-v1"
MAX_COMPONENTS = 8
MAX_NAME_LENGTH = 63
COMPONENT_KEYS = {
    "expected_sha256",
    "expected_size",
    "maximum_size",
    "name",
    "role",
    "source",
}
ROLE_VALUES = {
    "initial-image": "RIBON_BOOT_MODULE_ROLE_INITIAL_IMAGE",
    "auxiliary": "RIBON_BOOT_MODULE_ROLE_AUXILIARY",
}
PRODUCT_BUNDLE_CONTRACT = {
    "component_manifest_schema": MANIFEST_SCHEMA,
    "maximum_modules": MAX_COMPONENTS,
    "provider": "generated-component-bundle-v1",
}
PRODUCT_SERVICE_CONTRACT = {
    "id": "service.product.boot-module-bundle",
    "kind": "boot-module-bundle",
    "symbol": "ribon_generated_boot_module_bundle_service_descriptor",
}


def fail(message: str) -> None:
    """Raise one stable manifest validation error."""

    raise ValueError(message)


def _is_within(path: Path, root: Path) -> bool:
    """Return whether a resolved output path is owned by the build root."""

    try:
        path.resolve().relative_to(root.resolve())
    except ValueError:
        return False
    return True


def _logical_source(value: object) -> PurePosixPath:
    """Validate a manifest-relative source path without host-specific spelling."""

    if not isinstance(value, str) or not value or "\\" in value:
        fail("component source must be a non-empty POSIX relative path")
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        fail("component source must not be absolute or traverse directories")
    return path


def _read_exact(
    root: Path,
    logical_path: PurePosixPath,
    expected_size: int,
) -> bytes:
    """Open each path component without following links and read exact bytes."""

    nofollow = getattr(os, "O_NOFOLLOW", 0)
    directory_flag = getattr(os, "O_DIRECTORY", 0)
    close_on_exec = getattr(os, "O_CLOEXEC", 0)
    if nofollow == 0 or directory_flag == 0:
        fail("host does not provide no-follow component traversal")
    directory_fds: list[int] = []
    file_fd = -1
    try:
        directory_fd = os.open(
            root,
            os.O_RDONLY | directory_flag | nofollow | close_on_exec,
        )
        directory_fds.append(directory_fd)
        for part in logical_path.parts[:-1]:
            directory_fd = os.open(
                part,
                os.O_RDONLY | directory_flag | nofollow | close_on_exec,
                dir_fd=directory_fd,
            )
            directory_fds.append(directory_fd)
        file_fd = os.open(
            logical_path.parts[-1],
            os.O_RDONLY | nofollow | close_on_exec,
            dir_fd=directory_fds[-1],
        )
        with os.fdopen(file_fd, "rb", buffering=0) as stream:
            file_fd = -1
            before = os.fstat(stream.fileno())
            if not stat.S_ISREG(before.st_mode):
                fail(
                    "component source is not a regular file: "
                    f"{logical_path.as_posix()}"
                )
            data = stream.read(expected_size + 1)
            after = os.fstat(stream.fileno())
    except OSError as error:
        fail(
            "cannot read component source through symlink-free traversal "
            f"{logical_path.as_posix()}: {error}"
        )
    finally:
        if file_fd >= 0:
            os.close(file_fd)
        for descriptor in reversed(directory_fds):
            os.close(descriptor)
    if (
        before.st_dev != after.st_dev
        or before.st_ino != after.st_ino
        or before.st_size != after.st_size
        or before.st_mtime_ns != after.st_mtime_ns
        or before.st_size != expected_size
        or len(data) != expected_size
    ):
        fail(
            "component source size or identity changed: "
            f"{logical_path.as_posix()}"
        )
    return data


def _validate_manifest(path: Path) -> list[dict[str, object]]:
    """Load a complete exact component manifest in source order."""

    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        fail(f"cannot load component manifest: {error}")
    if (
        not isinstance(manifest, dict)
        or set(manifest) != {"components", "schema"}
        or manifest.get("schema") != MANIFEST_SCHEMA
    ):
        fail(f"manifest must use {MANIFEST_SCHEMA}")
    components = manifest.get("components")
    if not isinstance(components, list) or not 1 <= len(components) <= MAX_COMPONENTS:
        fail("component count must be in the range 1..8")

    normalized: list[dict[str, object]] = []
    names: set[str] = set()
    initial_images = 0
    for index, component in enumerate(components):
        if not isinstance(component, dict) or set(component) != COMPONENT_KEYS:
            fail(f"component {index} must define the complete v1 schema")
        name = component.get("name")
        role = component.get("role")
        digest = component.get("expected_sha256")
        expected_size = component.get("expected_size")
        maximum_size = component.get("maximum_size")
        if (
            not isinstance(name, str)
            or not 1 <= len(name) <= MAX_NAME_LENGTH
            or any(not (ch.isascii() and (ch.isalnum() or ch in "._-")) for ch in name)
            or name in names
        ):
            fail(f"component {index} has an invalid or duplicate logical name")
        if role not in ROLE_VALUES:
            fail(f"component {index} has an unknown role")
        if role == "initial-image":
            initial_images += 1
            if initial_images > 1:
                fail("initial-image role is a singleton")
        if (
            not isinstance(expected_size, int)
            or isinstance(expected_size, bool)
            or expected_size <= 0
            or not isinstance(maximum_size, int)
            or isinstance(maximum_size, bool)
            or maximum_size <= 0
            or expected_size > maximum_size
        ):
            fail(f"component {index} has an invalid exact or maximum size")
        if (
            not isinstance(digest, str)
            or len(digest) != 64
            or any(ch not in "0123456789abcdef" for ch in digest)
        ):
            fail(f"component {index} expected_sha256 must be lowercase SHA-256")
        logical_source = _logical_source(component.get("source"))
        data = _read_exact(path.parent, logical_source, expected_size)
        observed_digest = hashlib.sha256(data).hexdigest()
        if observed_digest != digest:
            fail(f"component {index} SHA-256 mismatch")
        names.add(name)
        normalized.append(
            {
                "data": data,
                "expected_sha256": digest,
                "maximum_size": maximum_size,
                "name": name,
                "role": role,
                "source": logical_source.as_posix(),
            }
        )
    return normalized


def _validate_product_manifest(path: Path) -> tuple[str, str]:
    """Bind component generation to one exact module-bearing product graph."""

    try:
        raw = path.read_bytes()
        product = json.loads(raw.decode("utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        fail(f"cannot load product manifest: {error}")
    services = product.get("services") if isinstance(product, dict) else None
    required = (
        product.get("required_capabilities") if isinstance(product, dict) else None
    )
    allowed = (
        product.get("allowed_capabilities") if isinstance(product, dict) else None
    )
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
    if (
        not isinstance(product, dict)
        or product.get("schema_version") != 1
        or product.get("product_kind") != "bootloader"
        or product.get("environment") != "raw-fdt"
        or product.get("boot_module_bundle") != PRODUCT_BUNDLE_CONTRACT
        or module_services != [PRODUCT_SERVICE_CONTRACT]
        or not isinstance(required, list)
        or not isinstance(allowed, list)
        or "BOOT_MODULE_BUNDLE" not in required
        or "BOOT_MODULE_BUNDLE" not in allowed
        or not isinstance(product.get("product_id"), str)
        or not product["product_id"]
    ):
        fail("product manifest does not authorize the exact boot-module bundle")
    return product["product_id"], hashlib.sha256(raw).hexdigest()


def _assembly(components: list[dict[str, object]]) -> str:
    """Render one architecture-neutral ELF assembly bundle."""

    lines = ["/* Generated by generate_boot_module_bundle.py. */"]
    for index, _component in enumerate(components):
        ordinal = f"{index:03d}"
        lines.extend(
            [
                f'.section .ribon.boot_modules.{ordinal}, "a"',
                ".balign 4096",
                f".global ribon_boot_module_{ordinal}_start",
                f"ribon_boot_module_{ordinal}_start:",
                f'.incbin "boot-module-components/{ordinal}.bin"',
                f".global ribon_boot_module_{ordinal}_end",
                f"ribon_boot_module_{ordinal}_end:",
                ".balign 4096",
                "",
            ]
        )
    return "\n".join(lines)


def _descriptors(components: list[dict[str, object]]) -> str:
    """Render the typed descriptor table consumed by Ribon Core."""

    declarations: list[str] = []
    entries: list[str] = []
    for index, component in enumerate(components):
        ordinal = f"{index:03d}"
        declarations.extend(
            [
                f"extern const unsigned char ribon_boot_module_{ordinal}_start[];",
                f"extern const unsigned char ribon_boot_module_{ordinal}_end[];",
            ]
        )
        entries.extend(
            [
                "    {",
                f'        .name = "{component["name"]}",',
                f"        .start = ribon_boot_module_{ordinal}_start,",
                f"        .end = ribon_boot_module_{ordinal}_end,",
                f"        .role = {ROLE_VALUES[str(component['role'])]},",
                "    },",
            ]
        )
    return "\n".join(
        [
            "/* Generated by generate_boot_module_bundle.py. */",
            "#include <Ribon/boot/module_bundle.h>",
            "#include <Ribon/service/directory.h>",
            "",
            "extern const unsigned char __ribon_boot_modules_start[];",
            "extern const unsigned char __ribon_boot_modules_end[];",
            *declarations,
            "",
            "static const struct RibonBootModuleComponent",
            "    ribon_generated_boot_module_components[] = {",
            *entries,
            "};",
            "",
            "const struct RibonBootModuleBundle ribon_generated_boot_module_bundle = {",
            "    .size = sizeof(ribon_generated_boot_module_bundle),",
            "    .abi_version = RIBON_BOOT_MODULE_BUNDLE_ABI_VERSION,",
            "    .components = ribon_generated_boot_module_components,",
            f"    .component_count = {len(components)}u,",
            "    .reserved = 0u,",
            "};",
            "",
            "static const struct RibonBootModuleBundleServiceOperations",
            "    ribon_generated_boot_module_bundle_operations = {",
            "    .size = sizeof(ribon_generated_boot_module_bundle_operations),",
            "    .abi_version = RIBON_SERVICE_ABI_VERSION,",
            "    .bundle = &ribon_generated_boot_module_bundle,",
            "    .section_start = __ribon_boot_modules_start,",
            "    .section_end = __ribon_boot_modules_end,",
            "};",
            "",
            "const struct RibonServiceDescriptor",
            "    ribon_generated_boot_module_bundle_service_descriptor = {",
            "    .magic = RIBON_SERVICE_DESCRIPTOR_MAGIC,",
            "    .size = sizeof(ribon_generated_boot_module_bundle_service_descriptor),",
            "    .abi_version = RIBON_SERVICE_ABI_VERSION,",
            "    .kind = RIBON_SERVICE_KIND_BOOT_MODULE_BUNDLE,",
            "    .cardinality = RIBON_SERVICE_CARDINALITY_AUTHORITY,",
            "    .lifetime = RIBON_SERVICE_LIFETIME_PERSISTENT,",
            "    .phase = RIBON_PLUGIN_PHASE_EARLY,",
            '    .id = "service.product.boot-module-bundle",',
            "    .provides = RIBON_CAP_BOOT_MODULE_BUNDLE,",
            "    .architecture_mask = RIBON_ARCH_MASK_AARCH64 | RIBON_ARCH_MASK_RISCV64,",
            "    .environment_mask = RIBON_ENV_MASK_RAW_FDT,",
            "    .mode_mask = RIBON_MODE_MASK_ALL,",
            "    .arena_budget = 1u,",
            "    .input_budget = 1u,",
            "    .output_budget = 1u,",
            "    .deadline_ms = 1u,",
            "    .operations = &ribon_generated_boot_module_bundle_operations,",
            "    .operations_size = sizeof(ribon_generated_boot_module_bundle_operations),",
            "    .operations_abi = RIBON_SERVICE_ABI_VERSION,",
            "    .validate_operations =",
            "        ribon_boot_module_bundle_service_operations_are_valid,",
            "};",
            "",
        ]
    )


def _bundle_digest(components: list[dict[str, object]]) -> str:
    """Bind order, logical identity, role, and exact bytes into one digest."""

    digest = hashlib.sha256()
    digest.update(PROVENANCE_SCHEMA.encode("ascii") + b"\0")
    for component in components:
        digest.update(str(component["name"]).encode("ascii") + b"\0")
        digest.update(str(component["role"]).encode("ascii") + b"\0")
        data = component["data"]
        assert isinstance(data, bytes)
        digest.update(len(data).to_bytes(8, "little"))
        digest.update(data)
    return digest.hexdigest()


def generate(
    manifest: Path,
    product_manifest: Path,
    output_root: Path,
    assembly: Path,
    descriptors: Path,
    provenance: Path,
) -> None:
    """Validate, snapshot, and emit one deterministic product component bundle."""

    outputs = (assembly, descriptors, provenance)
    if any(not _is_within(path, output_root) for path in outputs):
        fail("all generated outputs must be inside --output-root")
    if len({path.resolve() for path in outputs}) != len(outputs):
        fail("generated output paths must be distinct")
    components = _validate_manifest(manifest)
    product_id, product_manifest_sha256 = _validate_product_manifest(
        product_manifest
    )
    snapshot_dir = assembly.parent / "boot-module-components"
    if not _is_within(snapshot_dir, output_root):
        fail("component snapshot directory must be inside --output-root")
    if snapshot_dir.exists():
        shutil.rmtree(snapshot_dir)
    snapshot_dir.mkdir(parents=True)
    for index, component in enumerate(components):
        data = component["data"]
        assert isinstance(data, bytes)
        (snapshot_dir / f"{index:03d}.bin").write_bytes(data)

    assembly.parent.mkdir(parents=True, exist_ok=True)
    descriptors.parent.mkdir(parents=True, exist_ok=True)
    provenance.parent.mkdir(parents=True, exist_ok=True)
    assembly.write_text(_assembly(components), encoding="utf-8", newline="\n")
    descriptors.write_text(_descriptors(components), encoding="utf-8", newline="\n")
    records = []
    for index, component in enumerate(components):
        data = component["data"]
        assert isinstance(data, bytes)
        records.append(
            {
                "index": index,
                "maximum_size": component["maximum_size"],
                "name": component["name"],
                "role": component["role"],
                "sha256": component["expected_sha256"],
                "size": len(data),
                "snapshot": (
                    snapshot_dir / f"{index:03d}.bin"
                ).resolve().relative_to(output_root.resolve()).as_posix(),
                "source": component["source"],
            }
        )
    report = {
        "bundle_sha256": _bundle_digest(components),
        "component_count": len(components),
        "components": records,
        "product_id": product_id,
        "product_manifest_sha256": product_manifest_sha256,
        "schema": PROVENANCE_SCHEMA,
    }
    provenance.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def main() -> int:
    """CLI entry point."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--product-manifest", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--assembly", type=Path, required=True)
    parser.add_argument("--descriptors", type=Path, required=True)
    parser.add_argument("--provenance", type=Path, required=True)
    args = parser.parse_args()
    try:
        generate(
            args.manifest,
            args.product_manifest,
            args.output_root,
            args.assembly,
            args.descriptors,
            args.provenance,
        )
    except ValueError as error:
        print(f"RIBON-BOOT-MODULE-BUNDLE-FAIL: {error}", file=sys.stderr)
        return 1
    print("RIBON-BOOT-MODULE-BUNDLE-OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
