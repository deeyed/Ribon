#!/usr/bin/env python3
"""Prove UEFI fixture/external products do not share mutable build outputs."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


FIXTURE_PRODUCT = "x86_64-uefi-parus-fixture"
EXTERNAL_PRODUCT = "x86_64-uefi-parus-external"
FIXTURE_PRODUCT_ID = "bootmgr.x86_64-uefi-parus-fixture"
EXTERNAL_PRODUCT_ID = "bootmgr.x86_64-uefi-parus-external"
CANONICAL_OUTPUTS = (
    "BOOTX64.EFI",
    "esp/EFI/BOOT/BOOTX64.EFI",
    "esp/RIBON/BOOT.CFG",
    "esp/RIBON/PAYLOAD.ELF",
    "esp/RIBON/INIT.IMG",
    "generated/plugin_registry.c",
    "manifests/product.json",
    "results/object-graph.json",
)


class HermeticityError(RuntimeError):
    """Report one deterministic product-isolation failure."""


def sha256_file(path: Path) -> str:
    """Return one build artifact's immutable byte identity."""

    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_external_probe(path: Path, variant: int, fixture_marker: bool = False) -> None:
    """Write a minimal external-input ELF; it is never runtime evidence."""

    base = 0x00200000
    ident = b"\x7fELF" + bytes((2, 1, 1, 0)) + bytes(8)
    header = struct.pack(
        "<16sHHIQQQIHHHHHH",
        ident,
        2,
        62,
        1,
        base,
        64,
        0,
        0,
        64,
        56,
        1,
        0,
        0,
        0,
    )
    program = struct.pack(
        "<IIQQQQQQ",
        1,
        5,
        0x100,
        base,
        base,
        4,
        0x1000,
        0x1000,
    )
    marker = b"RIBON-FIXTURE-PAYLOAD-V1" if fixture_marker else b""
    path.write_bytes(
        header
        + program
        + bytes(0x100 - len(header) - len(program))
        + bytes((variant & 0xFF, 0xF4, 0xEB, 0xFD))
        + marker
    )


def run_make(
    make: str,
    root: Path,
    build_root: Path,
    target: str,
    log: Path,
    payload: Path | None = None,
    expect_success: bool = True,
) -> None:
    """Run one isolated Make target and preserve its complete output."""

    command = [make, "--no-print-directory", f"BUILD_ROOT={build_root}"]
    if payload is not None:
        command.append(f"UEFI_PARUS_PAYLOAD={payload}")
    command.append(target)
    environment = os.environ.copy()
    environment.pop("MAKEFLAGS", None)
    environment.pop("MFLAGS", None)
    completed = subprocess.run(
        command,
        cwd=root,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
        timeout=180,
    )
    with log.open("a", encoding="utf-8") as stream:
        stream.write("COMMAND " + " ".join(command) + "\n")
        stream.write(completed.stdout)
        stream.write(f"RETURN {completed.returncode}\n")
    if expect_success and completed.returncode != 0:
        tail = "\n".join(completed.stdout.splitlines()[-40:])
        raise HermeticityError(f"{target} failed:\n{tail}")
    if not expect_success and completed.returncode == 0:
        raise HermeticityError(f"{target} accepted a fixture-class external input")


def product_root(build_root: Path, product: str) -> Path:
    """Resolve one manifest-owned target root."""

    return build_root / "targets" / product


def snapshot(
    build_root: Path,
    product: str,
    expected_product_id: str,
    external_payload: Path | None = None,
) -> dict[str, str]:
    """Validate and hash one product's canonical reproducible outputs."""

    directory = product_root(build_root, product)
    outputs: dict[str, str] = {}
    for relative in CANONICAL_OUTPUTS:
        path = directory / relative
        if not path.is_file():
            raise HermeticityError(f"{product} is missing {relative}")
        outputs[relative] = sha256_file(path)
    map_path = directory / "ribon.map"
    if not map_path.is_file():
        raise HermeticityError(f"{product} is missing ribon.map")

    graph_path = directory / "results/object-graph.json"
    graph = json.loads(graph_path.read_text(encoding="utf-8"))
    if graph.get("product_id") != expected_product_id:
        raise HermeticityError(f"{product} generated the wrong product identity")
    manifest_path = directory / "manifests/product.json"
    if graph.get("source_manifest_sha256") != sha256_file(manifest_path):
        raise HermeticityError(f"{product} manifest digest is not closed")

    if external_payload is not None:
        validation_path = directory / "results/external-payload.json"
        if not validation_path.is_file():
            raise HermeticityError("external product has no payload provenance")
        validation = json.loads(validation_path.read_text(encoding="utf-8"))
        expected_payload_sha256 = sha256_file(external_payload)
        checks = {
            "validation-success": validation.get("success") is True,
            "validation-product":
                validation.get("product_id") == expected_product_id,
            "validation-payload-digest":
                validation.get("payload", {}).get("sha256")
                == expected_payload_sha256,
            "validation-payload-class":
                validation.get("payload", {}).get("class")
                == "external-kernel",
            "validation-payload-format":
                validation.get("payload", {}).get("format") == "elf64",
            "validation-payload-size":
                validation.get("payload", {}).get("size_bytes")
                == external_payload.stat().st_size,
            "esp-payload-digest":
                outputs["esp/RIBON/PAYLOAD.ELF"] == expected_payload_sha256,
        }
        failed = [name for name, passed in checks.items() if not passed]
        if failed:
            raise HermeticityError(
                "external payload provenance is not exact: "
                + ", ".join(failed)
            )
        outputs["results/external-payload.json"] = sha256_file(validation_path)
    return outputs


def require_equal(
    left: dict[str, str],
    right: dict[str, str],
    description: str,
) -> None:
    """Reject a changed product snapshot with one bounded diagnosis."""

    if left != right:
        changed = sorted(set(left) | set(right))
        changed = [key for key in changed if left.get(key) != right.get(key)]
        raise HermeticityError(f"{description} changed: {', '.join(changed)}")


def main(argv: list[str]) -> int:
    """Exercise both product orders, payload changes and independent roots."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make", default="make")
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--work-root", type=Path, required=True)
    args = parser.parse_args(argv)
    root = args.root.resolve()
    work_root = args.work_root.resolve()
    work_root.mkdir(parents=True, exist_ok=True)
    log = work_root / "commands.log"
    log.write_text("", encoding="utf-8")

    try:
        with tempfile.TemporaryDirectory(prefix="run-", dir=work_root) as raw:
            temporary = Path(raw)
            build_a = temporary / "build-a"
            build_b = temporary / "build-b"
            build_negative = temporary / "build-negative"
            probe_a = temporary / "external-a.elf"
            probe_b = temporary / "external-b.elf"
            fixture_probe = temporary / "fixture.elf"
            write_external_probe(probe_a, 0x90)
            write_external_probe(probe_b, 0x91)
            write_external_probe(fixture_probe, 0x92, fixture_marker=True)

            run_make(
                args.make,
                root,
                build_a,
                "x86_64-uefi-parus-fixture",
                log,
            )
            fixture_a_initial = snapshot(
                build_a,
                FIXTURE_PRODUCT,
                FIXTURE_PRODUCT_ID,
            )
            run_make(
                args.make,
                root,
                build_a,
                "x86_64-uefi-parus-external",
                log,
                probe_a,
            )
            external_a_initial = snapshot(
                build_a,
                EXTERNAL_PRODUCT,
                EXTERNAL_PRODUCT_ID,
                probe_a,
            )
            require_equal(
                fixture_a_initial,
                snapshot(build_a, FIXTURE_PRODUCT, FIXTURE_PRODUCT_ID),
                "fixture product after external build",
            )

            run_make(
                args.make,
                root,
                build_a,
                "x86_64-uefi-parus-external",
                log,
                probe_b,
            )
            external_a_final = snapshot(
                build_a,
                EXTERNAL_PRODUCT,
                EXTERNAL_PRODUCT_ID,
                probe_b,
            )
            if (
                external_a_initial["esp/RIBON/PAYLOAD.ELF"]
                == external_a_final["esp/RIBON/PAYLOAD.ELF"]
            ):
                raise HermeticityError("external payload change did not rebuild the ESP")
            require_equal(
                fixture_a_initial,
                snapshot(build_a, FIXTURE_PRODUCT, FIXTURE_PRODUCT_ID),
                "fixture product after external payload replacement",
            )

            run_make(
                args.make,
                root,
                build_a,
                "x86_64-uefi-parus-fixture",
                log,
            )
            require_equal(
                external_a_final,
                snapshot(
                    build_a,
                    EXTERNAL_PRODUCT,
                    EXTERNAL_PRODUCT_ID,
                    probe_b,
                ),
                "external product after fixture rebuild",
            )

            run_make(
                args.make,
                root,
                build_b,
                "x86_64-uefi-parus-external",
                log,
                probe_b,
            )
            external_b = snapshot(
                build_b,
                EXTERNAL_PRODUCT,
                EXTERNAL_PRODUCT_ID,
                probe_b,
            )
            run_make(
                args.make,
                root,
                build_b,
                "x86_64-uefi-parus-fixture",
                log,
            )
            fixture_b = snapshot(
                build_b,
                FIXTURE_PRODUCT,
                FIXTURE_PRODUCT_ID,
            )
            require_equal(
                fixture_a_initial,
                fixture_b,
                "fixture product across independent build roots",
            )
            require_equal(
                external_a_final,
                external_b,
                "external product across independent build roots",
            )

            fixture_paths = {
                str(product_root(build_a, FIXTURE_PRODUCT) / relative)
                for relative in CANONICAL_OUTPUTS
            }
            external_paths = {
                str(product_root(build_a, EXTERNAL_PRODUCT) / relative)
                for relative in CANONICAL_OUTPUTS
            }
            if fixture_paths & external_paths:
                raise HermeticityError("fixture and external products share output paths")
            if (
                fixture_a_initial["generated/plugin_registry.c"]
                == external_a_final["generated/plugin_registry.c"]
                or fixture_a_initial["esp/RIBON/PAYLOAD.ELF"]
                == external_a_final["esp/RIBON/PAYLOAD.ELF"]
            ):
                raise HermeticityError("product identity did not affect canonical outputs")

            run_make(
                args.make,
                root,
                build_negative,
                "x86_64-uefi-parus-external",
                log,
                fixture_probe,
                expect_success=False,
            )

            report = {
                "schema": "ribon-uefi-product-hermeticity-v1",
                "fixture_product": FIXTURE_PRODUCT_ID,
                "external_product": EXTERNAL_PRODUCT_ID,
                "fixture_digest": fixture_b["BOOTX64.EFI"],
                "external_digest": external_b["BOOTX64.EFI"],
                "external_payload_digest": sha256_file(probe_b),
                "build_orders": [
                    "fixture-external-fixture",
                    "external-fixture",
                ],
                "independent_build_roots": 2,
                "shared_writable_outputs": [],
                "fixture_external_rejection": True,
                "success": True,
            }
        (work_root / "result.json").write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    except (OSError, ValueError, subprocess.SubprocessError, HermeticityError) as error:
        print(f"RIBON-UEFI-PRODUCT-HERMETICITY-FAIL: {error}", file=sys.stderr)
        return 1
    print(
        "RIBON-UEFI-PRODUCT-HERMETICITY-OK "
        "products=2 orders=2 roots=2 shared-outputs=0"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
