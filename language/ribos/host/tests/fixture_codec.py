#!/usr/bin/env python3
"""Canonical host-only Ribos replay fixture codec."""

from __future__ import annotations

import hashlib
import struct
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

CONTEXT_HEADER_BYTES = 128
TRANSCRIPT_HEADER_BYTES = 192
TRANSCRIPT_ROW_BYTES = 128
INVALID_ID = 0xFFFFFFFF

CALLBACK_OK = 0
CALLBACK_POLICY_ERROR = 1
CALLBACK_CONTRACT_FAULT = 2

RESULT_NONE = 0
RESULT_VALUE = 1
RESULT_HANDLE = 2
RESULT_POLICY_ERROR = 3

JOURNAL_NONE = 0
JOURNAL_COMMITTED = 1
JOURNAL_PARTIAL = 2
JOURNAL_UNCERTAIN = 3

SECTION_TYPES = 1
SECTION_FUNCTIONS = 5
SECTION_SLOTS = 8
SECTION_INSTRUCTIONS = 9
SECTION_HELPER_IMPORTS = 11


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def u64(data: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", data, offset)[0]


@dataclass(frozen=True)
class Section:
    kind: int
    row_size: int
    count: int
    offset: int
    data: bytes

    def row(self, index: int) -> bytes:
        if index < 0 or index >= self.count:
            raise ValueError(f"section {self.kind}: row {index} out of range")
        start = index * self.row_size
        return self.data[start : start + self.row_size]


class Artifact:
    """Strict-enough read-only view used to construct bound host fixtures."""

    def __init__(self, data: bytes):
        if len(data) < 288 or data[:8] != b"RIBOSA1\0":
            raise ValueError("invalid Ribos artifact envelope")
        self.data = data
        self.payload_offset = u64(data, 24)
        self.payload_length = u64(data, 32)
        if (
            self.payload_offset != 128
            or self.payload_offset + self.payload_length > len(data)
        ):
            raise ValueError("invalid artifact payload range")
        self.payload = data[
            self.payload_offset : self.payload_offset + self.payload_length
        ]
        if self.payload[:8] != b"RIBBC01\0":
            raise ValueError("invalid Ribos bytecode payload")
        self.artifact_hash = data[72:104]
        if hashlib.sha256(self.payload).digest() != self.artifact_hash:
            raise ValueError("artifact hash mismatch")
        self.entry_function = u32(self.payload, 28)
        self.instruction_upper = u64(self.payload, 56)
        self.helper_upper = u64(self.payload, 72)
        self.stack_upper = u64(self.payload, 80)
        self.call_depth_upper = u32(self.payload, 88)
        section_count = u32(self.payload, 24)
        directory_offset = u64(self.payload, 128)
        directory_length = u64(self.payload, 136)
        if directory_length != section_count * 32:
            raise ValueError("invalid artifact section directory length")
        self.sections: dict[int, Section] = {}
        for index in range(section_count):
            descriptor = directory_offset + index * 32
            kind = u16(self.payload, descriptor)
            row_size = u32(self.payload, descriptor + 4)
            offset = u64(self.payload, descriptor + 8)
            length = u64(self.payload, descriptor + 16)
            count = u32(self.payload, descriptor + 24)
            if offset + length > len(self.payload):
                raise ValueError("artifact section out of range")
            if row_size != 0 and count * row_size != length:
                raise ValueError("artifact section shape mismatch")
            self.sections[kind] = Section(
                kind,
                row_size,
                count,
                offset,
                self.payload[offset : offset + length],
            )

    @classmethod
    def from_path(cls, path: Path) -> "Artifact":
        return cls(path.read_bytes())

    def section(self, kind: int) -> Section:
        try:
            return self.sections[kind]
        except KeyError as error:
            raise ValueError(f"missing artifact section {kind}") from error

    def type_by_name(self, name: str) -> tuple[int, int]:
        encoded = name.encode("ascii")
        for index in range(self.section(SECTION_TYPES).count):
            row = self.section(SECTION_TYPES).row(index)
            length = u32(row, 56)
            if row[60 : 60 + length] == encoded:
                return index, u32(row, 40)
        raise ValueError(f"missing artifact type {name}")

    def entry_context(self) -> tuple[int, int]:
        function = self.section(SECTION_FUNCTIONS).row(
            self.entry_function
        )
        if u32(function, 36) != 1:
            raise ValueError("entry function must have one context parameter")
        slot = self.section(SECTION_SLOTS).row(u32(function, 32))
        return u32(slot, 8), u32(slot, 16)

    def helper_imports(self) -> tuple[int, ...]:
        section = self.section(SECTION_HELPER_IMPORTS)
        return tuple(u32(section.row(index), 0) for index in range(section.count))

    def opcodes(self) -> set[int]:
        section = self.section(SECTION_INSTRUCTIONS)
        return {section.row(index)[0] for index in range(section.count)}


@dataclass(frozen=True)
class TranscriptRow:
    helper_id: int
    callback_status: int = CALLBACK_OK
    result_kind: int = RESULT_VALUE
    payload: bytes = b""
    operations: int = 1
    polls: int = 0
    elapsed_ns: int = 1
    journal_state: int = JOURNAL_NONE
    journal_digest: bytes = bytes(32)
    object_id: int = 0


def context_fixture(
    artifact: Artifact,
    *,
    mode: int = 0,
    phase: int = 0,
    generation: int = 1,
    payload: bytes | None = None,
) -> bytes:
    context_type, context_size = artifact.entry_context()
    if payload is None:
        payload = bytes(context_size)
    if len(payload) != context_size:
        raise ValueError("context payload does not match entry type size")
    header = bytearray(CONTEXT_HEADER_BYTES)
    header[:8] = b"RBCTX1\0\0"
    struct.pack_into("<HHIIIIIQ", header, 8, 1, 0, 128, 0, context_type,
                     mode, phase, generation)
    struct.pack_into("<QQQ", header, 40, 128, len(payload), 128 + len(payload))
    header[64:96] = hashlib.sha256(header[:64] + payload).digest()
    return bytes(header) + payload


def transcript_fixture(
    artifact: Artifact,
    context: bytes,
    rows: Iterable[TranscriptRow],
) -> bytes:
    row_list = tuple(rows)
    encoded_rows = bytearray(len(row_list) * TRANSCRIPT_ROW_BYTES)
    payload = bytearray()
    for index, item in enumerate(row_list):
        if len(item.journal_digest) != 32:
            raise ValueError("journal digest must be 32 bytes")
        if (item.journal_state == JOURNAL_NONE) != (
            item.journal_digest == bytes(32)
        ):
            raise ValueError("journal state and digest disagree")
        base = index * TRANSCRIPT_ROW_BYTES
        struct.pack_into(
            "<QIIIIQQQQQII",
            encoded_rows,
            base,
            index + 1,
            item.helper_id,
            item.callback_status,
            item.result_kind,
            0,
            item.operations,
            item.polls,
            item.elapsed_ns,
            len(payload),
            len(item.payload),
            item.journal_state,
            0,
        )
        encoded_rows[base + 72 : base + 104] = item.journal_digest
        struct.pack_into("<Q", encoded_rows, base + 104, item.object_id)
        payload.extend(item.payload)
    body = bytes(encoded_rows) + bytes(payload)
    header = bytearray(TRANSCRIPT_HEADER_BYTES)
    header[:8] = b"RBTRN1\0\0"
    struct.pack_into(
        "<HHIIIII",
        header,
        8,
        1,
        0,
        TRANSCRIPT_HEADER_BYTES,
        0,
        len(row_list),
        TRANSCRIPT_ROW_BYTES,
        0,
    )
    struct.pack_into(
        "<QQQQQ",
        header,
        32,
        TRANSCRIPT_HEADER_BYTES,
        len(encoded_rows),
        TRANSCRIPT_HEADER_BYTES + len(encoded_rows),
        len(payload),
        TRANSCRIPT_HEADER_BYTES + len(body),
    )
    header[72:104] = artifact.artifact_hash
    header[104:136] = context[64:96]
    header[136:168] = hashlib.sha256(body).digest()
    return bytes(header) + body


def compile_policy(
    compiler: Path,
    source: Path,
    artifact: Path,
) -> str:
    result = subprocess.run(
        [
            str(compiler),
            "--emit-artifact",
            str(artifact),
            str(source),
        ],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
    )
    return result.stdout


def run_policy(
    runner: Path,
    artifact: Path,
    context: Path,
    transcript: Path,
    *,
    check: bool = True,
    timeout: float = 10,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            str(runner),
            "--context",
            str(context),
            "--transcript",
            str(transcript),
            str(artifact),
        ],
        check=check,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
    )


def parse_report(text: str) -> dict[str, str]:
    report: dict[str, str] = {}
    for line in text.splitlines():
        key, separator, value = line.partition("=")
        if not separator or key in report:
            raise ValueError(f"invalid report line: {line!r}")
        report[key] = value
    if report.get("format") != "RIBOS-RUN-REPORT-V1":
        raise ValueError("unexpected Ribos run report format")
    return report
