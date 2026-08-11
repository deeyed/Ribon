#!/usr/bin/env python3
"""Stage the public Ribon SDK with deterministic content metadata."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import stat
import struct
import subprocess


def digest(path: Path) -> str:
    """Return the SHA-256 digest of one installed file."""

    return hashlib.sha256(path.read_bytes()).hexdigest()


def normalize_macho_uuid(path: Path) -> None:
    """Replace one Darwin LC_UUID with a digest-derived reproducible identity."""

    data = bytearray(path.read_bytes())
    if len(data) < 32 or data[:4] != b"\xcf\xfa\xed\xfe":
        return
    subprocess.run(
        ["codesign", "--remove-signature", str(path)],
        check=True,
        capture_output=True,
    )
    subprocess.run(
        ["strip", "-S", str(path)],
        check=True,
        capture_output=True,
    )
    data = bytearray(path.read_bytes())
    command_count, command_bytes = struct.unpack_from("<II", data, 16)
    if command_count > 4096 or command_bytes > len(data) - 32:
        raise ValueError(f"Mach-O load-command directory is invalid: {path}")
    cursor = 32
    end = cursor + command_bytes
    uuid_offset: int | None = None
    for _ in range(command_count):
        if cursor > end - 8:
            raise ValueError(f"Mach-O load command is truncated: {path}")
        command, size = struct.unpack_from("<II", data, cursor)
        if size < 8 or size > end - cursor:
            raise ValueError(f"Mach-O load command size is invalid: {path}")
        if command == 0x1B:
            if size != 24 or uuid_offset is not None:
                raise ValueError(f"Mach-O LC_UUID is noncanonical: {path}")
            uuid_offset = cursor + 8
        cursor += size
    if cursor != end or uuid_offset is None:
        raise ValueError(f"Mach-O LC_UUID is missing: {path}")
    data[uuid_offset:uuid_offset + 16] = bytes(16)
    identity = bytearray(hashlib.sha256(data).digest()[:16])
    identity[6] = (identity[6] & 0x0F) | 0x50
    identity[8] = (identity[8] & 0x3F) | 0x80
    data[uuid_offset:uuid_offset + 16] = identity
    path.write_bytes(data)
    subprocess.run(
        ["codesign", "--force", "--sign", "-", str(path)],
        check=True,
        capture_output=True,
    )


def named_path(value: str) -> tuple[str, Path]:
    """Parse one stable install name and source path without guessing either field."""

    name, separator, source = value.partition("=")
    if (
        separator != "="
        or not name
        or "/" in name
        or name in {".", ".."}
        or not source
    ):
        raise argparse.ArgumentTypeError("expected NAME=PATH with one basename NAME")
    return name, Path(source)


def main() -> int:
    """Copy only public headers, libraries, schemas, templates, and an ABI manifest."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--public-include", type=Path, required=True)
    parser.add_argument(
        "--public-ribos-include", type=Path, action="append", default=[]
    )
    parser.add_argument("--library", type=Path, action="append", required=True)
    parser.add_argument("--host-tool", type=named_path, action="append", default=[])
    parser.add_argument("--schemas", type=Path, required=True)
    parser.add_argument("--templates", type=Path, required=True)
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--sdk-abi", type=int, required=True)
    parser.add_argument("--core-abi", type=int, required=True)
    parser.add_argument("--plugin-abi-major", type=int, required=True)
    parser.add_argument("--plugin-abi-minor", type=int, required=True)
    parser.add_argument("--source-version", required=True)
    args = parser.parse_args()

    root = args.root
    if root.exists():
        shutil.rmtree(root)
    include_destination = root / "include" / "Ribon"
    library_destination = root / "lib"
    tool_destination = root / "bin"
    share_destination = root / "share" / "ribon"
    shutil.copytree(args.public_include, include_destination)
    ribos_include_destination = root / "include" / "ribos"
    for public_include in args.public_ribos_include:
        source = public_include / "ribos"
        if not source.is_dir():
            raise ValueError(f"public Ribos include root is invalid: {public_include}")
        shutil.copytree(source, ribos_include_destination, dirs_exist_ok=True)
    library_destination.mkdir(parents=True)
    for library in args.library:
        shutil.copy2(library, library_destination / library.name)
    tool_destination.mkdir(parents=True)
    installed_tools: dict[str, dict[str, object]] = {}
    tool_classes = {
        "ribosc": "compiler",
        "ribos-verify": "independent-verifier",
        "ribos-run": "host-runner",
        "ribon-compose-product": "product-composer",
        "ribon-update-manifest": "update-assembler-inspector",
        "ribon-update-layout": "layout-composer-inspector",
        "ribon-sign-policy": "offline-private-key-capable-signer",
    }
    if {name for name, _ in args.host_tool} != set(tool_classes):
        raise ValueError("host tool set does not match the versioned SDK tool contract")
    for name, source in sorted(args.host_tool):
        if not source.is_file():
            raise ValueError(f"host tool input is missing: {source}")
        destination = tool_destination / name
        shutil.copy2(source, destination)
        normalize_macho_uuid(destination)
        destination.chmod(destination.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        installed_tools[name] = {
            "class": tool_classes[name],
            "sha256": digest(destination),
            "target_linkable": False,
        }
    shutil.copytree(args.schemas, share_destination / "schemas")
    shutil.copytree(args.templates, share_destination / "templates")

    pkgconfig = library_destination / "pkgconfig"
    pkgconfig.mkdir()
    (pkgconfig / "ribon-sdk.pc").write_text(
        "prefix=${pcfiledir}/../..\n"
        "includedir=${prefix}/include\n"
        "libdir=${prefix}/lib\n"
        "\n"
        "Name: Ribon SDK\n"
        "Description: Deterministic boot and firmware plugin SDK\n"
        "Version: 0.4.0\n"
        "Cflags: -I${includedir}\n"
        "Libs: -L${libdir} -lribon-policy-ribos -lribon-update "
        "-lribon-sdk -lribon-boot -lribon-core -lribos-target-core\n",
        encoding="utf-8",
    )

    installed = sorted(path for path in root.rglob("*") if path.is_file())
    manifest = {
        "schema": "ribon-sdk-install-v2",
        "schema_version": 2,
        "sdk_abi": args.sdk_abi,
        "core_abi": args.core_abi,
        "plugin_abi": {
            "major": args.plugin_abi_major,
            "minor": args.plugin_abi_minor,
        },
        "source_version": args.source_version,
        "source_revision": args.source_revision,
        "boundaries": {
            "host_tools_target_linkable": False,
            "private_key_material_included": False,
            "target_libraries": sorted(library.name for library in args.library),
        },
        "host_tools": installed_tools,
        "files": {
            path.relative_to(root).as_posix(): digest(path)
            for path in installed
        },
    }
    share_destination.mkdir(parents=True, exist_ok=True)
    (share_destination / "sdk-manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
