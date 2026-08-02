#!/usr/bin/env python3
"""Validate one pinned Linux RISC-V64 Image and emit a loader descriptor."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile
import urllib.request


SCHEMA = "ribon-external-linux-riscv64-image-v1"
VALIDATION_SCHEMA = "ribon-external-linux-riscv64-image-validation-v1"
HEADER_SIZE = 64
EXPECTED_ENTRY_ABI = "riscv64-linux-fdt-v1"
EXPECTED_FORMAT = "linux-riscv64-image"
LINUX_RISCV64_MAGIC = b"RISCV\0\0\0"
LINUX_RISCV64_MAGIC2 = b"RSC\x05"
LINUX_RISCV64_TEXT_OFFSET = 2 * 1024 * 1024
LINUX_RISCV64_HEADER_VERSION = 2


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_descriptor(path: Path) -> dict[str, object]:
    """Load the exact external-input schema."""

    document = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(document, dict) or set(document) != {
        "artifact", "license", "provenance", "schema", "source"
    } or document.get("schema") != SCHEMA:
        raise ValueError(f"descriptor must use {SCHEMA}")
    source = document["source"]
    artifact = document["artifact"]
    license_info = document["license"]
    provenance = document["provenance"]
    if not isinstance(source, dict) or set(source) != {
        "distribution", "release", "target", "url"
    } or any(not isinstance(value, str) or not value for value in source.values()):
        raise ValueError("source identity is incomplete")
    if not str(source["url"]).startswith("https://"):
        raise ValueError("source URL must use HTTPS")
    if not isinstance(artifact, dict) or set(artifact) != {
        "architecture", "class", "expected_sha256", "expected_size",
        "filename", "maximum_size"
    }:
        raise ValueError("artifact identity is incomplete")
    digest = artifact.get("expected_sha256")
    expected_size = artifact.get("expected_size")
    maximum_size = artifact.get("maximum_size")
    if (
        artifact.get("architecture") != "riscv64"
        or artifact.get("class") != EXPECTED_FORMAT
        or artifact.get("filename") != "Image"
        or not isinstance(digest, str)
        or len(digest) != 64
        or any(ch not in "0123456789abcdef" for ch in digest)
        or not isinstance(expected_size, int)
        or isinstance(expected_size, bool)
        or expected_size <= HEADER_SIZE
        or not isinstance(maximum_size, int)
        or isinstance(maximum_size, bool)
        or maximum_size < expected_size
    ):
        raise ValueError("artifact contract is invalid")
    if not isinstance(license_info, dict) or set(license_info) != {
        "notice", "spdx"
    } or license_info.get("spdx") != "GPL-2.0-only":
        raise ValueError("license metadata is incomplete")
    if not isinstance(provenance, dict) or set(provenance) != {
        "kind", "verification"
    } or provenance.get("kind") != "pinned-upstream-release-artifact":
        raise ValueError("provenance metadata is incomplete")
    return document


def validate_image(data: bytes, descriptor: dict[str, object]) -> dict[str, int | str]:
    """Validate exact identity and the Linux RISC-V64 Image header class."""

    artifact = descriptor["artifact"]
    assert isinstance(artifact, dict)
    if len(data) != artifact["expected_size"]:
        raise ValueError("external Image exact size mismatch")
    if len(data) > artifact["maximum_size"]:
        raise ValueError("external Image exceeds maximum size")
    digest = _sha256(data)
    if digest != artifact["expected_sha256"]:
        raise ValueError("external Image SHA-256 mismatch")
    if (
        len(data) < HEADER_SIZE
        or data[48:56] != LINUX_RISCV64_MAGIC
        or data[56:60] != LINUX_RISCV64_MAGIC2
    ):
        raise ValueError("external input is not a Linux RISC-V64 raw Image")
    text_offset = int.from_bytes(data[8:16], "little")
    image_size = int.from_bytes(data[16:24], "little")
    flags = int.from_bytes(data[24:32], "little")
    version = int.from_bytes(data[32:36], "little")
    reserved0 = int.from_bytes(data[36:40], "little")
    reserved1 = int.from_bytes(data[40:48], "little")
    pe_header_offset = int.from_bytes(data[60:64], "little")
    pe_valid = (
        pe_header_offset == 0
        or (
            HEADER_SIZE <= pe_header_offset <= len(data) - 4
            and data[pe_header_offset : pe_header_offset + 4] == b"PE\0\0"
        )
    )
    if (
        text_offset != LINUX_RISCV64_TEXT_OFFSET
        or image_size < len(data)
        or image_size > artifact["maximum_size"]
        or flags != 0
        or version != LINUX_RISCV64_HEADER_VERSION
        or reserved0 != 0
        or reserved1 != 0
        or not pe_valid
    ):
        raise ValueError("Linux RISC-V64 Image header contract mismatch")
    return {
        "class": EXPECTED_FORMAT,
        "flags": flags,
        "header_version": version,
        "image_size": image_size,
        "pe_header_offset": pe_header_offset,
        "sha256": digest,
        "size": len(data),
        "text_offset": text_offset,
    }


def validate_product(path: Path, descriptor: dict[str, object]) -> dict[str, object]:
    """Bind the external input to one typed product placement contract."""

    product = json.loads(path.read_text(encoding="utf-8"))
    payload = product.get("payload") if isinstance(product, dict) else None
    if (
        not isinstance(product, dict)
        or product.get("architecture") != "riscv64"
        or product.get("environment") != "raw-fdt"
        or product.get("boot_protocols") != ["linux"]
        or not isinstance(payload, dict)
        or payload.get("architecture") != "riscv64"
        or payload.get("format") != EXPECTED_FORMAT
        or payload.get("entry_abi") != EXPECTED_ENTRY_ABI
        or not isinstance(payload.get("load_base"), int)
        or payload["load_base"] <= 0
        or payload["load_base"] % LINUX_RISCV64_TEXT_OFFSET != 0
        or not isinstance(payload.get("load_size"), int)
        or payload["load_size"] <= 0
    ):
        raise ValueError("product does not select the Linux RISC-V64 Image contract")
    artifact = descriptor["artifact"]
    assert isinstance(artifact, dict)
    if payload["load_size"] < artifact["maximum_size"]:
        raise ValueError("product placement is smaller than the input maximum")
    return product


def fetch(url: str, destination: Path) -> None:
    """Download to a sibling temporary file and publish atomically."""

    destination.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(
        prefix=destination.name + ".", dir=destination.parent
    )
    try:
        with os.fdopen(descriptor, "wb") as output, urllib.request.urlopen(
            url, timeout=60
        ) as response:
            while chunk := response.read(1024 * 1024):
                output.write(chunk)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, destination)
    except Exception:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--descriptor", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--product-manifest", type=Path, required=True)
    parser.add_argument("--assembly", type=Path, required=True)
    parser.add_argument("--result", type=Path, required=True)
    parser.add_argument("--allow-download", action="store_true")
    args = parser.parse_args()

    descriptor = load_descriptor(args.descriptor)
    product = validate_product(args.product_manifest, descriptor)
    source = descriptor["source"]
    assert isinstance(source, dict)
    if not args.cache.is_file():
        if not args.allow_download:
            raise ValueError("pinned external Image is missing from the build cache")
        fetch(str(source["url"]), args.cache)
    validation = validate_image(args.cache.read_bytes(), descriptor)
    payload = product["payload"]
    assert isinstance(payload, dict)
    assembly = "\n".join(
        (
            "/* Generated by prepare_external_linux_riscv64_image.py. */",
            ".globl ribon_embedded_payload",
            f".set ribon_embedded_payload, {payload['load_base']}",
            ".section .rodata.ribon_payload_descriptor,\"a\",@progbits",
            ".balign 8",
            ".globl ribon_embedded_payload_size",
            ".type ribon_embedded_payload_size, %object",
            "ribon_embedded_payload_size:",
            f"  .quad {validation['size']}",
            ".size ribon_embedded_payload_size, 8",
            "",
        )
    )
    report = {
        "schema": VALIDATION_SCHEMA,
        "descriptor": {
            "path": str(args.descriptor),
            "sha256": _sha256(args.descriptor.read_bytes()),
        },
        "product": {
            "id": product["product_id"],
            "manifest_sha256": _sha256(args.product_manifest.read_bytes()),
        },
        "source": source,
        "license": descriptor["license"],
        "provenance": descriptor["provenance"],
        "artifact": {
            "path": str(args.cache),
            **validation,
            "architecture": "riscv64",
        },
        "placement": {"base": payload["load_base"], "size": payload["load_size"]},
    }
    args.assembly.parent.mkdir(parents=True, exist_ok=True)
    args.result.parent.mkdir(parents=True, exist_ok=True)
    args.assembly.write_text(assembly, encoding="utf-8")
    args.result.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(
        "RIBON-EXTERNAL-LINUX-RISCV64-IMAGE-OK "
        f"sha256={validation['sha256']} size={validation['size']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
