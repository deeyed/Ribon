#!/usr/bin/env python3
"""Exercise deterministic D02 host tooling and hostile input corpus."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def run(command: list[str], succeed: bool = True) -> subprocess.CompletedProcess[str]:
    """Run one command and require its selected success class."""

    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if (result.returncode == 0) != succeed:
        raise RuntimeError(
            f"unexpected command result {result.returncode}: {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def write_json(path: Path, value: object) -> None:
    """Write one hostile JSON input in a temporary test directory."""

    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> int:
    """Verify reproducibility, D01 integration and independent metadata parsing."""

    parser = argparse.ArgumentParser()
    parser.add_argument("--c-codec", type=Path, required=True)
    parser.add_argument("--layout-tool", type=Path, required=True)
    parser.add_argument("--manifest-tool", type=Path, required=True)
    parser.add_argument("--layout-source", type=Path, required=True)
    parser.add_argument("--manifest-source", type=Path, required=True)
    args = parser.parse_args()
    python = sys.executable

    with tempfile.TemporaryDirectory(prefix="ribon-d02-") as directory:
        root = Path(directory)
        first = root / "layout-first.json"
        second = root / "layout-second.json"
        manifest = root / "manifest.bin"
        metadata = root / "metadata.bin"
        c_identity = root / "layout-c.bin"
        host_identity = root / "layout-host.bin"
        inspection = root / "metadata.json"
        run(
            [
                python,
                str(args.manifest_tool),
                "assemble",
                "--source",
                str(args.manifest_source),
                "--output",
                str(manifest),
            ]
        )
        compose = [
            python,
            str(args.layout_tool),
            "compose",
            "--source",
            str(args.layout_source),
            "--manifest",
            str(manifest),
            "--output",
        ]
        run(compose + [str(first), "--identity-output", str(host_identity)])
        run(compose + [str(second)])
        run([str(args.c_codec), "--emit-layout", str(c_identity)])
        if first.read_bytes() != second.read_bytes():
            raise RuntimeError("layout provenance is not byte reproducible")
        if c_identity.read_bytes() != host_identity.read_bytes():
            raise RuntimeError("C and host layout identity encoders disagree")
        report = json.loads(first.read_text(encoding="utf-8"))
        if (
            report["layout_identity_sha256"]
            != "1e1a7f165fa068dc048189919b6e4de7d28a31710b82451d0bcbb86c8396342a"
            or report["source_sha256"]
            != "9120b28dab9525dc602f65cb31bd45039590464f147bdaf058ecd4594347ea3a"
            or report["manifest"]["required_slot_bytes"] != 1048576
            or len(report["regions"]) != 11
            or report["regions"][-1]["offset"] + report["regions"][-1]["length"]
            != report["media_capacity_bytes"]
        ):
            raise RuntimeError("canonical layout provenance vector drifted")

        source = json.loads(args.layout_source.read_text(encoding="utf-8"))
        hostile = root / "hostile.json"
        source["allocation_alignment"] = 3
        write_json(hostile, source)
        hostile_compose = [
            python,
            str(args.layout_tool),
            "compose",
            "--source",
            str(hostile),
            "--manifest",
            str(manifest),
            "--output",
            str(root / "bad.json"),
        ]
        run(hostile_compose, False)
        source = json.loads(args.layout_source.read_text(encoding="utf-8"))
        source["media_capacity_bytes"] = 4096
        write_json(hostile, source)
        run(hostile_compose, False)
        source = json.loads(args.layout_source.read_text(encoding="utf-8"))
        source["unexpected"] = 1
        write_json(hostile, source)
        run(hostile_compose, False)

        oversized = bytearray(manifest.read_bytes())
        struct.pack_into("<Q", oversized, 512 + 144, (1 << 64) - 1)
        oversized_path = root / "oversized-manifest.bin"
        oversized_path.write_bytes(oversized)
        run(
            [
                python,
                str(args.layout_tool),
                "compose",
                "--source",
                str(args.layout_source),
                "--manifest",
                str(oversized_path),
                "--output",
                str(root / "oversized.json"),
            ],
            False,
        )

        run([str(args.c_codec), "--emit-metadata", str(metadata)])
        run(
            [
                python,
                str(args.layout_tool),
                "inspect-metadata",
                "--metadata",
                str(metadata),
                "--output",
                str(inspection),
            ]
        )
        metadata_report = json.loads(inspection.read_text(encoding="utf-8"))
        if (
            metadata_report["active_slot"] != 0
            or metadata_report["pending_slot"] is not None
            or metadata_report["slots"][0]["state"] != 4
            or metadata_report["slots"][1]["state"] != 0
        ):
            raise RuntimeError("independent metadata semantics drifted")
        corrupted = root / "metadata-corrupted.bin"
        contents = bytearray(metadata.read_bytes())
        contents[64] ^= 1
        corrupted.write_bytes(contents)
        run(
            [
                python,
                str(args.layout_tool),
                "inspect-metadata",
                "--metadata",
                str(corrupted),
                "--output",
                str(root / "corrupted.json"),
            ],
            False,
        )
        truncated = root / "metadata-truncated.bin"
        truncated.write_bytes(metadata.read_bytes()[:-1])
        run(
            [
                python,
                str(args.layout_tool),
                "inspect-metadata",
                "--metadata",
                str(truncated),
                "--output",
                str(root / "truncated.json"),
            ],
            False,
        )
    print(
        "RIBON-UPDATE-STORAGE-TOOL-V1-OK reproducible=1 manifest=d01 metadata=independent"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
