#!/usr/bin/env python3
"""Validate the pinned FreeBSD amd64 release image and publish provenance."""

from __future__ import annotations

import argparse
import hashlib
import json
import lzma
from pathlib import Path
import urllib.request


EXPECTED_URL = (
    "https://download.freebsd.org/releases/amd64/amd64/ISO-IMAGES/15.1/"
    "FreeBSD-15.1-RELEASE-amd64-mini-memstick.img.xz"
)
EXPECTED_CHECKSUM_URL = (
    "https://www.freebsd.org/releases/15.1R/checksums/"
    "CHECKSUM.SHA256-FreeBSD-15.1-RELEASE-amd64.asc"
)
COMPRESSED_SIZE = 116377572
COMPRESSED_SHA256 = "25602a32253ed7cbbb50007d43c48e2f4342b92985465a79d8808a2156179b3f"
RAW_SIZE = 678842880
RAW_SHA256 = "61c4a454eb799bc92fcef375d434c0d48721951c598e4cb91b5aa8faa30d7a40"
CHECKSUM_SIZE = 2053
CHECKSUM_SHA256 = "801924b52ac5614686486a602055d1ec8d5034545d2572e4fd390fab782a2946"


def sha256_file(path: Path) -> tuple[int, str]:
    """Return exact size and SHA-256 without loading a release image in memory."""

    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            size += len(chunk)
            digest.update(chunk)
    return size, digest.hexdigest()


def validate_descriptor(path: Path) -> dict[str, object]:
    """Require one closed descriptor for the selected production release."""

    descriptor = json.loads(path.read_text(encoding="utf-8"))
    artifact = descriptor.get("artifact") if isinstance(descriptor, dict) else None
    keys = {
        "architecture", "checksum_sha256", "checksum_size", "checksum_source",
        "class", "compressed_sha256", "compressed_size", "license",
        "maximum_compressed_size", "raw_sha256", "raw_size", "source", "version",
    }
    if (
        set(descriptor) != {"artifact", "schema"}
        or descriptor.get("schema") != "ribon-external-freebsd-v1"
        or not isinstance(artifact, dict)
        or set(artifact) != keys
        or artifact.get("architecture") != "x86_64"
        or artifact.get("class") != "freebsd-amd64-mini-memstick"
        or artifact.get("version") != "FreeBSD-15.1-RELEASE"
        or artifact.get("source") != EXPECTED_URL
        or artifact.get("checksum_source") != EXPECTED_CHECKSUM_URL
        or artifact.get("compressed_size") != COMPRESSED_SIZE
        or artifact.get("compressed_sha256") != COMPRESSED_SHA256
        or artifact.get("raw_size") != RAW_SIZE
        or artifact.get("raw_sha256") != RAW_SHA256
        or artifact.get("checksum_size") != CHECKSUM_SIZE
        or artifact.get("checksum_sha256") != CHECKSUM_SHA256
        or artifact.get("maximum_compressed_size") != 134217728
    ):
        raise ValueError("external FreeBSD descriptor is not the pinned contract")
    return descriptor


def download_exact(url: str, destination: Path, maximum: int) -> None:
    """Download into a private temporary path and publish only validated bytes."""

    temporary = destination.with_name(destination.name + ".download")
    digest = hashlib.sha256()
    size = 0
    try:
        temporary.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(url, timeout=30) as response, temporary.open("wb") as out:
            while True:
                chunk = response.read(1024 * 1024)
                if not chunk:
                    break
                size += len(chunk)
                if size > maximum:
                    raise ValueError("download exceeds the descriptor bound")
                digest.update(chunk)
                out.write(chunk)
        if size != COMPRESSED_SIZE or digest.hexdigest() != COMPRESSED_SHA256:
            raise ValueError("downloaded FreeBSD image identity is invalid")
        temporary.replace(destination)
    finally:
        if temporary.exists():
            temporary.unlink()


def decompress_exact(source: Path, destination: Path) -> None:
    """Decompress XZ transactionally and reject short, trailing or corrupt input."""

    temporary = destination.with_name(destination.name + ".decompress")
    digest = hashlib.sha256()
    size = 0
    try:
        destination.parent.mkdir(parents=True, exist_ok=True)
        with lzma.open(source, "rb") as inp, temporary.open("wb") as out:
            while True:
                chunk = inp.read(1024 * 1024)
                if not chunk:
                    break
                size += len(chunk)
                if size > RAW_SIZE:
                    raise ValueError("decompressed FreeBSD image exceeds its exact size")
                digest.update(chunk)
                out.write(chunk)
        if size != RAW_SIZE or digest.hexdigest() != RAW_SHA256:
            raise ValueError("decompressed FreeBSD image identity is invalid")
        temporary.replace(destination)
    finally:
        if temporary.exists():
            temporary.unlink()


def validate_disk_layout(path: Path) -> dict[str, int]:
    """Validate the official MBR, EFI FAT32 partition and FreeBSD slice bounds."""

    with path.open("rb") as stream:
        mbr = stream.read(512)
        if len(mbr) != 512 or mbr[510:512] != b"\x55\xaa":
            raise ValueError("FreeBSD image has no valid MBR")
        entries = []
        for index in range(4):
            offset = 446 + index * 16
            entries.append({
                "type": mbr[offset + 4],
                "start": int.from_bytes(mbr[offset + 8:offset + 12], "little"),
                "sectors": int.from_bytes(mbr[offset + 12:offset + 16], "little"),
            })
        efi, freebsd = entries[0], entries[1]
        if efi != {"type": 0xEF, "start": 1, "sectors": 66584}:
            raise ValueError("FreeBSD image EFI partition changed")
        if freebsd != {"type": 0xA5, "start": 66585, "sectors": 1259280}:
            raise ValueError("FreeBSD image root slice changed")
        if (freebsd["start"] + freebsd["sectors"]) * 512 > RAW_SIZE:
            raise ValueError("FreeBSD partition table exceeds the image")
        stream.seek(efi["start"] * 512)
        bpb = stream.read(512)
        if len(bpb) != 512 or bpb[82:90] != b"FAT32   " or bpb[510:512] != b"\x55\xaa":
            raise ValueError("FreeBSD EFI partition is not the expected FAT32 volume")
    return {
        "efi_start_lba": efi["start"],
        "efi_sectors": efi["sectors"],
        "freebsd_start_lba": freebsd["start"],
        "freebsd_sectors": freebsd["sectors"],
    }


def main() -> int:
    """Validate/download both compressed and raw release authorities."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--descriptor", type=Path, required=True)
    parser.add_argument("--compressed-cache", type=Path, required=True)
    parser.add_argument("--raw-cache", type=Path, required=True)
    parser.add_argument("--result", type=Path, required=True)
    parser.add_argument("--allow-download", action="store_true")
    args = parser.parse_args()
    validate_descriptor(args.descriptor)
    downloaded = False
    if not args.compressed_cache.is_file():
        if not args.allow_download:
            raise ValueError("validated FreeBSD cache is absent and download is disabled")
        download_exact(EXPECTED_URL, args.compressed_cache, 134217728)
        downloaded = True
    if sha256_file(args.compressed_cache) != (COMPRESSED_SIZE, COMPRESSED_SHA256):
        raise ValueError("cached compressed FreeBSD image does not match the descriptor")
    if not args.raw_cache.is_file():
        decompress_exact(args.compressed_cache, args.raw_cache)
    if sha256_file(args.raw_cache) != (RAW_SIZE, RAW_SHA256):
        raise ValueError("cached raw FreeBSD image does not match the descriptor")
    layout = validate_disk_layout(args.raw_cache)
    report = {
        "artifact": {
            "architecture": "x86_64",
            "class": "freebsd-amd64-mini-memstick",
            "compressed_sha256": COMPRESSED_SHA256,
            "compressed_size": COMPRESSED_SIZE,
            "raw_sha256": RAW_SHA256,
            "raw_size": RAW_SIZE,
            "version": "FreeBSD-15.1-RELEASE",
        },
        "checksum_authority": {
            "pgp_signed": True,
            "sha256": CHECKSUM_SHA256,
            "signature_verified": False,
            "size": CHECKSUM_SIZE,
            "source": EXPECTED_CHECKSUM_URL,
        },
        "descriptor_sha256": sha256_file(args.descriptor)[1],
        "downloaded": downloaded,
        "layout": layout,
        "schema": "ribon-external-freebsd-validation-v1",
    }
    args.result.parent.mkdir(parents=True, exist_ok=True)
    args.result.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"RIBON-FREEBSD-INPUT-OK sha256={RAW_SHA256} size={RAW_SIZE}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
