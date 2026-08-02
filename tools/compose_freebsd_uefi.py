#!/usr/bin/env python3
"""Compose a deterministic Ribon overlay into an official FreeBSD FAT32 ESP."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil


RAW_SIZE = 678842880
RAW_SHA256 = "61c4a454eb799bc92fcef375d434c0d48721951c598e4cb91b5aa8faa30d7a40"
FREEBSD_LOADER_MARKER = b"FreeBSD/amd64 EFI loader, Revision 3.0"
END_OF_CHAIN = 0x0FFFFFFF


def sha256_file(path: Path) -> str:
    """Hash one artifact without unbounded allocation."""

    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_pe(data: bytes, marker: bytes | None = None) -> dict[str, int]:
    """Reject anything other than a bounded x86_64 EFI application."""

    if len(data) < 0x100 or data[:2] != b"MZ":
        raise ValueError("payload is not a DOS-wrapped PE image")
    pe = int.from_bytes(data[0x3C:0x40], "little")
    if pe > len(data) - 0x78 or data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("payload has no bounded PE signature")
    optional = pe + 24
    machine = int.from_bytes(data[pe + 4:pe + 6], "little")
    magic = int.from_bytes(data[optional:optional + 2], "little")
    entry_rva = int.from_bytes(data[optional + 16:optional + 20], "little")
    subsystem = int.from_bytes(data[optional + 68:optional + 70], "little")
    if machine != 0x8664 or magic != 0x20B or entry_rva == 0 or subsystem != 10:
        raise ValueError("payload is not an x86_64 PE32+ EFI application")
    if marker is not None and marker not in data:
        raise ValueError("official FreeBSD loader identity marker is absent")
    return {"entry_rva": entry_rva, "machine": machine, "subsystem": subsystem}


def short_name(component: str) -> bytes:
    """Encode one canonical ASCII 8.3 component without path traversal."""

    if not component or component in (".", "..") or "/" in component or "\\" in component:
        raise ValueError("non-canonical FAT path component")
    if component.count(".") > 1:
        raise ValueError("FAT path component has multiple dots")
    stem, dot, extension = component.partition(".")
    allowed = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$%'-@~`!(){}^#&"
    stem = stem.upper()
    extension = extension.upper()
    if not 1 <= len(stem) <= 8 or len(extension) > 3:
        raise ValueError("FAT path component exceeds 8.3")
    if any(character not in allowed for character in stem + extension):
        raise ValueError("FAT path component contains a forbidden byte")
    return stem.encode("ascii").ljust(8, b" ") + extension.encode("ascii").ljust(3, b" ")


class Fat32Image:
    """Bounded FAT32 reader/writer for one already validated ESP partition."""

    def __init__(self, image: Path):
        self.image = image
        self.stream = image.open("r+b")
        mbr = self._read(0, 512)
        entry = mbr[446:462]
        self.partition_lba = int.from_bytes(entry[8:12], "little")
        self.partition_sectors = int.from_bytes(entry[12:16], "little")
        if mbr[510:512] != b"\x55\xaa" or entry[4] != 0xEF or self.partition_lba != 1:
            raise ValueError("composed image has no canonical EFI MBR partition")
        self.partition_offset = self.partition_lba * 512
        bpb = self._read(self.partition_offset, 512)
        self.bytes_per_sector = int.from_bytes(bpb[11:13], "little")
        self.sectors_per_cluster = bpb[13]
        self.reserved_sectors = int.from_bytes(bpb[14:16], "little")
        self.fat_count = bpb[16]
        self.total_sectors = int.from_bytes(bpb[32:36], "little")
        self.sectors_per_fat = int.from_bytes(bpb[36:40], "little")
        self.root_cluster = int.from_bytes(bpb[44:48], "little")
        self.fsinfo_sector = int.from_bytes(bpb[48:50], "little")
        self.backup_boot_sector = int.from_bytes(bpb[50:52], "little")
        if (
            self.bytes_per_sector != 512
            or self.sectors_per_cluster == 0
            or self.reserved_sectors < 2
            or self.fat_count != 2
            or self.total_sectors != self.partition_sectors
            or self.sectors_per_fat == 0
            or self.root_cluster < 2
            or bpb[82:90] != b"FAT32   "
            or bpb[510:512] != b"\x55\xaa"
        ):
            raise ValueError("unsupported or malformed FAT32 boot sector")
        self.cluster_size = self.bytes_per_sector * self.sectors_per_cluster
        self.fat_offset = self.partition_offset + self.reserved_sectors * self.bytes_per_sector
        self.fat_size = self.sectors_per_fat * self.bytes_per_sector
        self.data_offset = self.fat_offset + self.fat_count * self.fat_size
        data_sectors = self.total_sectors - self.reserved_sectors - self.fat_count * self.sectors_per_fat
        self.cluster_count = data_sectors // self.sectors_per_cluster
        self.max_cluster = self.cluster_count + 1
        self.fat = bytearray(self._read(self.fat_offset, self.fat_size))
        if self._read(self.fat_offset + self.fat_size, self.fat_size) != self.fat:
            raise ValueError("FAT copies differ before composition")

    def close(self) -> None:
        """Close the composed image after explicit flush."""

        self.stream.close()

    def _read(self, offset: int, size: int) -> bytes:
        if offset < 0 or size < 0 or offset > RAW_SIZE - size:
            raise ValueError("image read exceeds its bounded range")
        self.stream.seek(offset)
        data = self.stream.read(size)
        if len(data) != size:
            raise ValueError("short image read")
        return data

    def _write(self, offset: int, data: bytes) -> None:
        if offset < 0 or offset > RAW_SIZE - len(data):
            raise ValueError("image write exceeds its bounded range")
        self.stream.seek(offset)
        if self.stream.write(data) != len(data):
            raise ValueError("short image write")

    def fat_entry(self, cluster: int) -> int:
        if cluster < 2 or cluster > self.max_cluster:
            raise ValueError("FAT cluster is out of range")
        return int.from_bytes(self.fat[cluster * 4:cluster * 4 + 4], "little") & 0x0FFFFFFF

    def set_fat_entry(self, cluster: int, value: int) -> None:
        if cluster < 2 or cluster > self.max_cluster or value < 0 or value > 0x0FFFFFFF:
            raise ValueError("FAT update is out of range")
        offset = cluster * 4
        old = int.from_bytes(self.fat[offset:offset + 4], "little")
        self.fat[offset:offset + 4] = ((old & 0xF0000000) | value).to_bytes(4, "little")

    def cluster_offset(self, cluster: int) -> int:
        if cluster < 2 or cluster > self.max_cluster:
            raise ValueError("data cluster is out of range")
        return self.data_offset + (cluster - 2) * self.cluster_size

    def chain(self, start: int) -> list[int]:
        """Return one acyclic bounded cluster chain."""

        clusters = []
        seen = set()
        cluster = start
        while True:
            if cluster in seen or len(clusters) >= self.cluster_count:
                raise ValueError("cyclic or oversized FAT chain")
            seen.add(cluster)
            clusters.append(cluster)
            following = self.fat_entry(cluster)
            if following >= 0x0FFFFFF8:
                return clusters
            if following < 2 or following == 0x0FFFFFF7:
                raise ValueError("FAT chain terminates in an invalid cluster")
            cluster = following

    def directory_entries(self, cluster: int) -> list[tuple[int, bytes]]:
        """Return physical offsets and short entries from one directory."""

        entries = []
        end_seen = False
        for member in self.chain(cluster):
            base = self.cluster_offset(member)
            data = self._read(base, self.cluster_size)
            for relative in range(0, self.cluster_size, 32):
                entry = data[relative:relative + 32]
                if end_seen:
                    entries.append((base + relative, bytes(32)))
                else:
                    entries.append((base + relative, entry))
                    if entry[0] == 0:
                        end_seen = True
        return entries

    @staticmethod
    def entry_cluster(entry: bytes) -> int:
        return int.from_bytes(entry[20:22], "little") << 16 | int.from_bytes(entry[26:28], "little")

    def find(self, path: str) -> tuple[int, bytes]:
        """Resolve one absolute canonical 8.3 path without following LFN aliases."""

        if not path.startswith("/") or path.endswith("/") or "//" in path:
            raise ValueError("non-canonical FAT path")
        components = [short_name(component) for component in path[1:].split("/")]
        directory = self.root_cluster
        for index, name in enumerate(components):
            matches = [
                (offset, entry)
                for offset, entry in self.directory_entries(directory)
                if entry[0] not in (0, 0xE5)
                and entry[11] != 0x0F
                and entry[:11] == name
            ]
            if len(matches) != 1:
                raise ValueError("FAT path is missing or duplicated")
            offset, entry = matches[0]
            if index + 1 != len(components):
                if entry[11] & 0x10 == 0:
                    raise ValueError("FAT path crosses a non-directory")
                directory = self.entry_cluster(entry)
            else:
                return offset, entry
        raise ValueError("empty FAT path")

    def read_file(self, path: str) -> bytes:
        """Read one exact regular file through its bounded chain."""

        _, entry = self.find(path)
        if entry[11] & 0x10:
            raise ValueError("FAT path names a directory")
        size = int.from_bytes(entry[28:32], "little")
        if size == 0:
            raise ValueError("zero-size FAT file")
        cluster = self.entry_cluster(entry)
        data = bytearray()
        for member in self.chain(cluster):
            data += self._read(self.cluster_offset(member), self.cluster_size)
            if len(data) >= size:
                return bytes(data[:size])
        raise ValueError("FAT file chain is short")

    def allocate(self, count: int, reusable: list[int] | None = None) -> list[int]:
        """Allocate exact first-fit clusters, reusing an owned chain first."""

        if count <= 0 or count > self.cluster_count:
            raise ValueError("invalid FAT allocation request")
        reusable = list(reusable or [])
        selected = reusable[:count]
        for cluster in range(2, self.max_cluster + 1):
            if len(selected) == count:
                break
            if cluster not in reusable and self.fat_entry(cluster) == 0:
                selected.append(cluster)
        if len(selected) != count:
            raise ValueError("FAT volume has insufficient free clusters")
        for index, cluster in enumerate(selected):
            self.set_fat_entry(cluster, selected[index + 1] if index + 1 < count else END_OF_CHAIN)
        for cluster in reusable[count:]:
            self.set_fat_entry(cluster, 0)
            self._write(self.cluster_offset(cluster), bytes(self.cluster_size))
        return selected

    def write_clusters(self, clusters: list[int], data: bytes) -> None:
        """Write exact content and zero every unused byte in its last cluster."""

        capacity = len(clusters) * self.cluster_size
        if not data or len(data) > capacity:
            raise ValueError("FAT content does not fit its allocation")
        padded = data + bytes(capacity - len(data))
        for index, cluster in enumerate(clusters):
            start = index * self.cluster_size
            self._write(self.cluster_offset(cluster), padded[start:start + self.cluster_size])

    @staticmethod
    def make_entry(name: bytes, attributes: int, cluster: int, size: int) -> bytes:
        """Create a deterministic short directory entry dated 2026-06-16."""

        if len(name) != 11 or cluster < 2 or size < 0 or size > 0xFFFFFFFF:
            raise ValueError("invalid FAT directory entry")
        entry = bytearray(32)
        entry[:11] = name
        entry[11] = attributes
        fat_date = ((2026 - 1980) << 9) | (6 << 5) | 16
        entry[14:16] = (0).to_bytes(2, "little")
        entry[16:18] = fat_date.to_bytes(2, "little")
        entry[18:20] = fat_date.to_bytes(2, "little")
        entry[20:22] = (cluster >> 16).to_bytes(2, "little")
        entry[22:24] = (0).to_bytes(2, "little")
        entry[24:26] = fat_date.to_bytes(2, "little")
        entry[26:28] = (cluster & 0xFFFF).to_bytes(2, "little")
        entry[28:32] = size.to_bytes(4, "little")
        return bytes(entry)

    def free_directory_slot(self, directory: int) -> int:
        """Return the first deterministic free directory entry, extending if needed."""

        for offset, entry in self.directory_entries(directory):
            if entry[0] in (0, 0xE5):
                return offset
        chain = self.chain(directory)
        extension = self.allocate(1)
        self.set_fat_entry(chain[-1], extension[0])
        self._write(self.cluster_offset(extension[0]), bytes(self.cluster_size))
        return self.cluster_offset(extension[0])

    def ensure_directory(self, parent: int, component: str) -> int:
        """Find or create one deterministic 8.3 directory."""

        name = short_name(component)
        matches = [
            entry for _, entry in self.directory_entries(parent)
            if entry[0] not in (0, 0xE5) and entry[11] != 0x0F and entry[:11] == name
        ]
        if len(matches) > 1:
            raise ValueError("duplicate FAT directory")
        if matches:
            if matches[0][11] & 0x10 == 0:
                raise ValueError("FAT directory name is occupied by a file")
            return self.entry_cluster(matches[0])
        cluster = self.allocate(1)[0]
        content = bytearray(self.cluster_size)
        content[:32] = self.make_entry(b".          ", 0x10, cluster, 0)
        content[32:64] = self.make_entry(b"..         ", 0x10, parent, 0)
        self._write(self.cluster_offset(cluster), content)
        self._write(self.free_directory_slot(parent), self.make_entry(name, 0x10, cluster, 0))
        return cluster

    def put_file(self, directory: int, component: str, data: bytes) -> None:
        """Create or replace one regular 8.3 file with exact bytes."""

        name = short_name(component)
        matches = [
            (offset, entry) for offset, entry in self.directory_entries(directory)
            if entry[0] not in (0, 0xE5) and entry[11] != 0x0F and entry[:11] == name
        ]
        if len(matches) > 1:
            raise ValueError("duplicate FAT file")
        if matches and matches[0][1][11] & 0x10:
            raise ValueError("FAT file name is occupied by a directory")
        reusable = self.chain(self.entry_cluster(matches[0][1])) if matches else []
        count = (len(data) + self.cluster_size - 1) // self.cluster_size
        clusters = self.allocate(count, reusable)
        self.write_clusters(clusters, data)
        offset = matches[0][0] if matches else self.free_directory_slot(directory)
        self._write(offset, self.make_entry(name, 0x20, clusters[0], len(data)))

    def flush(self) -> None:
        """Publish both FAT copies and deterministic FSInfo hints."""

        for index in range(self.fat_count):
            self._write(self.fat_offset + index * self.fat_size, self.fat)
        free = [cluster for cluster in range(2, self.max_cluster + 1) if self.fat_entry(cluster) == 0]
        for sector in (self.fsinfo_sector, self.backup_boot_sector + 1):
            offset = self.partition_offset + sector * self.bytes_per_sector
            info = bytearray(self._read(offset, self.bytes_per_sector))
            if info[0:4] == b"RRaA" and info[484:488] == b"rrAa" and info[508:512] == b"\0\0\x55\xaa":
                info[488:492] = len(free).to_bytes(4, "little")
                info[492:496] = (free[0] if free else END_OF_CHAIN).to_bytes(4, "little")
                self._write(offset, info)
        self.stream.flush()


def main() -> int:
    """Create one immutable-input-derived FreeBSD product disk and provenance."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--ribon-app", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--loader-output", type=Path, required=True)
    parser.add_argument("--result", type=Path, required=True)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        raise ValueError("official FreeBSD input cannot be modified in place")
    if args.source.stat().st_size != RAW_SIZE or sha256_file(args.source) != RAW_SHA256:
        raise ValueError("official FreeBSD raw image identity is invalid")
    application = args.ribon_app.read_bytes()
    config = args.config.read_bytes()
    app_pe = validate_pe(application)
    if not 1 <= len(config) <= 4096 or b"kernel=/EFI/FREEBSD/LOADER.EFI\n" not in config:
        raise ValueError("FreeBSD Ribon boot config is not exact or bounded")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.source, args.output)
    fat = Fat32Image(args.output)
    try:
        loader_before = fat.read_file("/EFI/BOOT/BOOTX64.EFI")
        loader_pe = validate_pe(loader_before, FREEBSD_LOADER_MARKER)
        efi_directory = fat.entry_cluster(fat.find("/EFI")[1])
        boot_directory = fat.entry_cluster(fat.find("/EFI/BOOT")[1])
        freebsd_directory = fat.ensure_directory(efi_directory, "FREEBSD")
        fat.put_file(freebsd_directory, "LOADER.EFI", loader_before)
        fat.put_file(boot_directory, "BOOTX64.EFI", application)
        ribon_directory = fat.ensure_directory(fat.root_cluster, "RIBON")
        fat.put_file(ribon_directory, "BOOT.CFG", config)
        fat.flush()
        if fat.read_file("/EFI/FREEBSD/LOADER.EFI") != loader_before:
            raise ValueError("official FreeBSD loader changed during composition")
        if fat.read_file("/EFI/BOOT/BOOTX64.EFI") != application:
            raise ValueError("composed image does not contain the exact Ribon application")
        if fat.read_file("/RIBON/BOOT.CFG") != config:
            raise ValueError("composed image does not contain the exact boot config")
    finally:
        fat.close()
    if sha256_file(args.source) != RAW_SHA256:
        raise ValueError("official FreeBSD source image mutated during composition")
    args.loader_output.parent.mkdir(parents=True, exist_ok=True)
    args.loader_output.write_bytes(loader_before)
    report = {
        "composed": {
            "sha256": sha256_file(args.output),
            "size": args.output.stat().st_size,
        },
        "files": {
            "/EFI/BOOT/BOOTX64.EFI": {
                "sha256": hashlib.sha256(application).hexdigest(),
                "size": len(application),
            },
            "/EFI/FREEBSD/LOADER.EFI": {
                "pe": loader_pe,
                "sha256": hashlib.sha256(loader_before).hexdigest(),
                "size": len(loader_before),
            },
            "/RIBON/BOOT.CFG": {
                "sha256": hashlib.sha256(config).hexdigest(),
                "size": len(config),
            },
        },
        "official_source": {
            "immutable": True,
            "sha256": RAW_SHA256,
            "size": RAW_SIZE,
        },
        "ribon_pe": app_pe,
        "schema": "ribon-freebsd-uefi-package-v1",
    }
    args.result.parent.mkdir(parents=True, exist_ok=True)
    args.result.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(
        "RIBON-FREEBSD-PACKAGE-OK "
        f"loader={report['files']['/EFI/FREEBSD/LOADER.EFI']['sha256']} "
        f"composed={report['composed']['sha256']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
