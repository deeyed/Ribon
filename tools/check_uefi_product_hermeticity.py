#!/usr/bin/env python3
"""Prove fixture, external, Linux and FreeBSD UEFI products are hermetic."""

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
LINUX_PRODUCT = "x86_64-uefi-linux"
LINUX_PRODUCT_ID = "bootmgr.x86_64-uefi-linux"
FREEBSD_PRODUCT = "x86_64-uefi-freebsd"
FREEBSD_PRODUCT_ID = "bootmgr.x86_64-uefi-freebsd"
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
LINUX_CANONICAL_OUTPUTS = (
    "BOOTX64.EFI",
    "esp/EFI/BOOT/BOOTX64.EFI",
    "esp/RIBON/BOOT.CFG",
    "esp/RIBON/LINUX.EFI",
    "esp/RIBON/INITRD.CPIO",
    "generated/plugin_registry.c",
    "manifests/product.json",
    "results/object-graph.json",
    "results/external-linux-efi.json",
    "results/initramfs-components.json",
)
FREEBSD_CANONICAL_OUTPUTS = (
    "BOOTX64.EFI",
    "FreeBSD-15.1-Ribon-amd64.img",
    "overlay/BOOT.CFG",
    "payload/loader.efi",
    "generated/plugin_registry.c",
    "manifests/product.json",
    "results/object-graph.json",
    "results/external-freebsd.json",
    "results/package.json",
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
    linux_cache: Path | None = None,
    freebsd_compressed_cache: Path | None = None,
    freebsd_raw_cache: Path | None = None,
    expect_success: bool = True,
) -> None:
    """Run one isolated Make target and preserve its complete output."""

    command = [make, "--no-print-directory", f"BUILD_ROOT={build_root}"]
    if payload is not None:
        command.append(f"UEFI_PARUS_PAYLOAD={payload}")
    if linux_cache is not None:
        command.append(f"UEFI_LINUX_CACHE={linux_cache}")
    if freebsd_compressed_cache is not None:
        command.append(
            f"UEFI_FREEBSD_COMPRESSED_CACHE={freebsd_compressed_cache}"
        )
    if freebsd_raw_cache is not None:
        command.append(f"UEFI_FREEBSD_RAW_CACHE={freebsd_raw_cache}")
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
    canonical_outputs: tuple[str, ...] = CANONICAL_OUTPUTS,
) -> dict[str, str]:
    """Validate and hash one product's canonical reproducible outputs."""

    directory = product_root(build_root, product)
    outputs: dict[str, str] = {}
    for relative in canonical_outputs:
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


def validate_freebsd_snapshot(
    build_root: Path,
    outputs: dict[str, str],
) -> None:
    """Close the composed disk, official loader and source provenance identity."""

    directory = product_root(build_root, FREEBSD_PRODUCT)
    package = json.loads(
        (directory / "results/package.json").read_text(encoding="utf-8")
    )
    external = json.loads(
        (directory / "results/external-freebsd.json").read_text(
            encoding="utf-8"
        )
    )
    checks = {
        "package-schema": package.get("schema") == "ribon-freebsd-uefi-package-v1",
        "composed-disk": package.get("composed", {}).get("sha256")
            == outputs["FreeBSD-15.1-Ribon-amd64.img"],
        "official-loader": package.get("files", {}).get(
            "/EFI/FREEBSD/LOADER.EFI", {}
        ).get("sha256")
            == outputs["payload/loader.efi"],
        "external-schema": external.get("schema")
            == "ribon-external-freebsd-validation-v1",
        "external-raw-source": external.get("artifact", {}).get("raw_sha256")
            == package.get("official_source", {}).get("sha256"),
        "external-pgp-presence": external.get("checksum_authority", {}).get(
            "pgp_signed"
        ) is True,
        "external-signature-nonclaim": external.get(
            "checksum_authority", {}
        ).get("signature_verified") is False,
    }
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise HermeticityError(
            "FreeBSD package provenance is not exact: " + ", ".join(failed)
        )


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
    """Exercise product orders, input changes and independent build roots."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make", default="make")
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--work-root", type=Path, required=True)
    parser.add_argument("--linux-cache", type=Path, required=True)
    parser.add_argument("--freebsd-compressed-cache", type=Path, required=True)
    parser.add_argument("--freebsd-raw-cache", type=Path, required=True)
    args = parser.parse_args(argv)
    root = args.root.resolve()
    linux_cache = args.linux_cache.resolve()
    if not linux_cache.is_file():
        raise SystemExit(f"validated Linux EFI cache is absent: {linux_cache}")
    freebsd_compressed_cache = args.freebsd_compressed_cache.resolve()
    freebsd_raw_cache = args.freebsd_raw_cache.resolve()
    if not freebsd_compressed_cache.is_file() or not freebsd_raw_cache.is_file():
        raise SystemExit(
            "validated FreeBSD caches are absent: "
            f"{freebsd_compressed_cache}, {freebsd_raw_cache}"
        )
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

            run_make(
                args.make,
                root,
                build_a,
                "x86_64-uefi-linux-product",
                log,
                linux_cache=linux_cache,
            )
            linux_a = snapshot(
                build_a,
                LINUX_PRODUCT,
                LINUX_PRODUCT_ID,
                canonical_outputs=LINUX_CANONICAL_OUTPUTS,
            )
            require_equal(
                fixture_a_initial,
                snapshot(build_a, FIXTURE_PRODUCT, FIXTURE_PRODUCT_ID),
                "fixture product after Linux build",
            )
            require_equal(
                external_a_final,
                snapshot(
                    build_a,
                    EXTERNAL_PRODUCT,
                    EXTERNAL_PRODUCT_ID,
                    probe_b,
                ),
                "external product after Linux build",
            )

            run_make(
                args.make,
                root,
                build_b,
                "x86_64-uefi-linux-product",
                log,
                linux_cache=linux_cache,
            )
            linux_b = snapshot(
                build_b,
                LINUX_PRODUCT,
                LINUX_PRODUCT_ID,
                canonical_outputs=LINUX_CANONICAL_OUTPUTS,
            )
            require_equal(
                linux_a,
                linux_b,
                "Linux product across independent build roots",
            )

            run_make(
                args.make,
                root,
                build_a,
                "x86_64-uefi-freebsd-product",
                log,
                freebsd_compressed_cache=freebsd_compressed_cache,
                freebsd_raw_cache=freebsd_raw_cache,
            )
            freebsd_a = snapshot(
                build_a,
                FREEBSD_PRODUCT,
                FREEBSD_PRODUCT_ID,
                canonical_outputs=FREEBSD_CANONICAL_OUTPUTS,
            )
            validate_freebsd_snapshot(build_a, freebsd_a)
            require_equal(
                linux_a,
                snapshot(
                    build_a,
                    LINUX_PRODUCT,
                    LINUX_PRODUCT_ID,
                    canonical_outputs=LINUX_CANONICAL_OUTPUTS,
                ),
                "Linux product after FreeBSD build",
            )

            run_make(
                args.make,
                root,
                build_b,
                "x86_64-uefi-freebsd-product",
                log,
                freebsd_compressed_cache=freebsd_compressed_cache,
                freebsd_raw_cache=freebsd_raw_cache,
            )
            freebsd_b = snapshot(
                build_b,
                FREEBSD_PRODUCT,
                FREEBSD_PRODUCT_ID,
                canonical_outputs=FREEBSD_CANONICAL_OUTPUTS,
            )
            validate_freebsd_snapshot(build_b, freebsd_b)
            require_equal(
                freebsd_a,
                freebsd_b,
                "FreeBSD product across independent build roots",
            )

            all_paths = [
                {
                    str(product_root(build_a, FIXTURE_PRODUCT) / relative)
                    for relative in CANONICAL_OUTPUTS
                },
                {
                    str(product_root(build_a, EXTERNAL_PRODUCT) / relative)
                    for relative in CANONICAL_OUTPUTS
                },
                {
                    str(product_root(build_a, LINUX_PRODUCT) / relative)
                    for relative in LINUX_CANONICAL_OUTPUTS
                },
                {
                    str(product_root(build_a, FREEBSD_PRODUCT) / relative)
                    for relative in FREEBSD_CANONICAL_OUTPUTS
                },
            ]
            if any(
                all_paths[left] & all_paths[right]
                for left in range(len(all_paths))
                for right in range(left + 1, len(all_paths))
            ):
                raise HermeticityError("UEFI products share writable output paths")

            report = {
                "schema": "ribon-uefi-product-hermeticity-v1",
                "fixture_product": FIXTURE_PRODUCT_ID,
                "external_product": EXTERNAL_PRODUCT_ID,
                "linux_product": LINUX_PRODUCT_ID,
                "freebsd_product": FREEBSD_PRODUCT_ID,
                "fixture_digest": fixture_b["BOOTX64.EFI"],
                "external_digest": external_b["BOOTX64.EFI"],
                "external_payload_digest": sha256_file(probe_b),
                "linux_payload_digest": linux_b["esp/RIBON/LINUX.EFI"],
                "freebsd_loader_digest": freebsd_b["payload/loader.efi"],
                "freebsd_disk_digest":
                    freebsd_b["FreeBSD-15.1-Ribon-amd64.img"],
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
        "products=4 orders=2 roots=2 shared-outputs=0"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
