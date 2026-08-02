#!/usr/bin/env python3
"""Build one recovery/update consumer using only the installed Ribon SDK."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import struct
import sys


def sha256(path: Path) -> str:
    """Return one exact file digest."""

    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command: list[str], cwd: Path) -> str:
    """Run one bounded host build step and preserve a useful first divergence."""

    result = subprocess.run(command, cwd=cwd, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        raise ValueError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"{result.stdout}{result.stderr}"
        )
    return result.stdout


def write_json(path: Path, value: object) -> None:
    """Write deterministic presentation JSON."""

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


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
        raise ValueError("external consumer Mach-O directory is invalid")
    cursor = 32
    end = cursor + command_bytes
    uuid_offset: int | None = None
    for _ in range(command_count):
        if cursor > end - 8:
            raise ValueError("external consumer Mach-O command is truncated")
        command, size = struct.unpack_from("<II", data, cursor)
        if size < 8 or size > end - cursor:
            raise ValueError("external consumer Mach-O command size is invalid")
        if command == 0x1B:
            if size != 24 or uuid_offset is not None:
                raise ValueError("external consumer LC_UUID is noncanonical")
            uuid_offset = cursor + 8
        cursor += size
    if cursor != end or uuid_offset is None:
        raise ValueError("external consumer LC_UUID is missing")
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


def require_dependencies(
    path: Path,
    allowed_roots: tuple[Path, ...],
    working_directory: Path,
) -> None:
    """Reject any user header dependency outside installed SDK or consumer root."""

    text = path.read_text(encoding="utf-8").replace("\\\n", " ")
    fields = text.split(":", 1)[1].split()
    for spelling in fields:
        dependency = Path(spelling.replace("\\ ", " "))
        if not dependency.is_absolute():
            dependency = working_directory / dependency
        dependency = dependency.resolve()
        if not any(dependency.is_relative_to(root) for root in allowed_roots):
            raise ValueError(f"source-private dependency escaped installed SDK: {dependency}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-root", type=Path, required=True)
    parser.add_argument("--work-root", type=Path, required=True)
    parser.add_argument("--cc", required=True)
    args = parser.parse_args()

    install = args.install_root.resolve()
    work = args.work_root.resolve()
    if not (install / "share/ribon/sdk-manifest.json").is_file():
        raise ValueError("installed SDK manifest is missing")
    template = install / "share/ribon/templates/deployment-consumer"
    if not template.is_dir() or work == install or install.is_relative_to(work):
        raise ValueError("deployment consumer template or work root is invalid")
    if work.exists():
        shutil.rmtree(work)
    shutil.copytree(template, work)
    generated = work / "generated"
    results = work / "results"
    generated.mkdir()
    results.mkdir()

    tools = install / "bin"
    product = work / "product.json"
    registry = generated / "registry.c"
    graph = results / "object-graph.json"
    run(
        [
            str(tools / "ribon-compose-product"),
            "--manifest", str(product),
            "--architecture", "x86_64",
            "--output", str(registry),
            "--report", str(graph),
        ],
        work,
    )

    policy = generated / "policy.rba"
    run([str(tools / "ribosc"), "--emit-artifact", str(policy), "policy.rbs"], work)
    run([str(tools / "ribos-verify"), str(policy)], work)

    payload = generated / "kernel.payload"
    payload.write_bytes(bytes((index * 17 + 3) & 0xFF for index in range(4096)))
    update_source = generated / "update-source.json"
    write_json(
        update_source,
        {
            "architecture_id": "architecture.x86_64",
            "bundle_generation": 2,
            "components": [
                {
                    "bundle_offset": 0,
                    "destination_class": "kernel-slot",
                    "destination_id": "slot.inactive.kernel",
                    "entry_contract_id": "entry.x86_64.direct-v1",
                    "expected_sha256": sha256(payload),
                    "image_format": "elf64",
                    "logical_id": "system.kernel",
                    "maximum_size": 8192,
                    "required": True,
                    "role": "kernel",
                    "source": "kernel.payload",
                }
            ],
            "creation_policy_version": 1,
            "environment_id": "environment.host",
            "hardware_revision": {"maximum": 1, "minimum": 0},
            "manifest_schema_id": "ribon.update.manifest.v1",
            "mode": "recovery",
            "platform_id": "platform.external-consumer",
            "predecessor_generation": 1,
            "product_digest_sha256": sha256(product),
            "product_id": "sdk.external-recovery-update",
            "protocol": {"id": "protocol.synthetic", "major": 1, "minor": 0},
            "rollback_domain": "ribon.update.external-consumer.v1",
            "rollback_sequence": 2,
            "schema": "ribon-update-manifest-source-v1",
        },
    )
    manifest = generated / "update.man"
    run(
        [str(tools / "ribon-update-manifest"), "assemble", "--source",
         str(update_source), "--output", str(manifest)],
        work,
    )
    inspection = run(
        [str(tools / "ribon-update-manifest"), "inspect", "--manifest", str(manifest)],
        work,
    )
    inspection_value = json.loads(inspection)
    if inspection_value.get("component_count") != 1 or inspection_value.get("mode") != 2:
        raise ValueError("installed update inspector returned the wrong recovery contract")

    include = install / "include"
    reproducible_flags = [
        f"-ffile-prefix-map={install}=sdk",
        f"-ffile-prefix-map={work}=consumer",
        f"-fdebug-prefix-map={install}=sdk",
        f"-fdebug-prefix-map={work}=consumer",
    ]
    registry_object = generated / "registry.o"
    registry_dep = generated / "registry.d"
    run(
        [args.cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
         *reproducible_flags, "-MMD", "-MF", str(registry_dep),
         "-I", str(include), "-c",
         str(registry), "-o", str(registry_object)],
        work,
    )
    executable = generated / "external-recovery-update"
    main_dep = generated / "main.d"
    run(
        [args.cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
         *reproducible_flags, "-MMD", "-MF", str(main_dep),
         "-I", str(include), "main.c",
         str(install / "lib/libribon-update.a"),
         str(install / "lib/libribon-sdk.a"),
         str(install / "lib/libribon-boot.a"),
         str(install / "lib/libribon-core.a"), "-o", str(executable)],
        work,
    )
    normalize_macho_uuid(executable)
    allowed = (install, work)
    require_dependencies(registry_dep, allowed, work)
    require_dependencies(main_dep, allowed, work)
    registry_dep.unlink()
    main_dep.unlink()
    output = run([str(executable), str(manifest)], work)
    if output.count("RIBON-D08-EXTERNAL-DEPLOYMENT-CONSUMER-OK") != 1:
        raise ValueError("external consumer terminal receipt was not unique")

    sdk_manifest = json.loads(
        (install / "share/ribon/sdk-manifest.json").read_text(encoding="utf-8")
    )
    report = {
        "schema": "ribon-sdk-deployment-consumer-v1",
        "source_revision": sdk_manifest["source_revision"],
        "evidence_class": "host-build/unit",
        "source_private_dependencies": 0,
        "target_firmware_executed": False,
        "artifacts": {
            "executable_sha256": sha256(executable),
            "manifest_sha256": sha256(manifest),
            "object_graph_sha256": sha256(graph),
            "policy_sha256": sha256(policy),
            "registry_object_sha256": sha256(registry_object),
        },
    }
    write_json(results / "consumer.json", report)
    print(
        "RIBON-D08-EXTERNAL-DEPLOYMENT-CONSUMER-OK "
        "product=recovery-update source-private=0 evidence=host-build/unit"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"RIBON-D08-EXTERNAL-DEPLOYMENT-CONSUMER-FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
