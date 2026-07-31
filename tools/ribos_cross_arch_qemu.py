#!/usr/bin/env python3
"""Execute confirmed and trial Ribos policies through three QEMU targets."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time


CORE_RECEIPT = (
    "RIBOS-R18-CORE-COMMIT-OK "
    "receipt=v1-stage8-action21-helpers4-fallback0"
)
MARKERS = (
    "RIBOS-R18-QEMU-ENTRY",
    "RIBOS-R18-ARTIFACT-OPEN-OK",
    "RIBOS-R18-TRANSACTION-PREPARED",
    "RIBOS-R18-POLICY-EXECUTE",
    "RIBOS-R18-SIGNED-AUTH-OK",
    CORE_RECEIPT,
    "RIBOS-R18-SIGNATURE-FALLBACK-OK",
    "RIBOS-R18-CORRUPT-FALLBACK-OK",
    "RIBOS-R18-TRUNCATION-FALLBACK-OK",
    "RIBOS-R18-PRODUCT-FALLBACK-OK",
    "RIBOS-R18-SCHEMA-FALLBACK-OK",
    "RIBOS-R18-KEY-FALLBACK-OK",
    "RIBOS-R18-SEQUENCE-FALLBACK-OK",
    "RIBOS-R18-STATE-FALLBACK-OK",
    "RIBOS-R18-BUDGET-FALLBACK-OK",
    "RIBOS-R18-DEADLINE-FALLBACK-OK",
    "RIBOS-R18-TRIAL-CONFIRM-OK",
    "RIBOS-R18-TRIAL-ROLLBACK-OK",
    "RIBOS-R18-NETWORK-ABSENT-OK",
    "RIBOS-R18-QEMU-VALIDATION-OK",
)
FORBIDDEN_MARKERS = (
    "RIBOS-R18-QEMU-FAIL",
    "RIBON-PANIC",
    "PARUS:PANIC",
    "Unhandled Exception",
    "ASSERT_EFI_ERROR",
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_tree(path: Path) -> str:
    digest = hashlib.sha256()
    for item in sorted(entry for entry in path.rglob("*") if entry.is_file()):
        digest.update(item.relative_to(path).as_posix().encode("utf-8"))
        digest.update(b"\0")
        digest.update(bytes.fromhex(sha256_file(item)))
    return digest.hexdigest()


def qemu_version(binary: str) -> str:
    completed = subprocess.run(
        [binary, "--version"],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=3,
    )
    lines = completed.stdout.decode("utf-8", errors="replace").splitlines()
    return lines[0] if lines else "unavailable"


def commands(args: argparse.Namespace) -> dict[str, list[str]]:
    return {
        "amd64": [
            args.qemu_x86_64,
            "-machine", "q35",
            "-m", "256M",
            "-display", "none",
            "-serial", "stdio",
            "-monitor", "none",
            "-net", "none",
            "-no-reboot",
            "-no-shutdown",
            "-drive",
            f"if=pflash,format=raw,readonly=on,file={args.x86_64_firmware}",
            "-snapshot",
            "-drive", f"format=raw,file=fat:{args.x86_64_esp}",
        ],
        "aarch64": [
            args.qemu_aarch64,
            "-machine", "virt",
            "-cpu", "cortex-a72",
            "-m", "256M",
            "-nographic",
            "-monitor", "none",
            "-net", "none",
            "-no-reboot",
            "-no-shutdown",
            "-kernel", str(args.aarch64_image),
        ],
        "riscv64": [
            args.qemu_riscv64,
            "-machine", "virt",
            "-m", "256M",
            "-nographic",
            "-monitor", "none",
            "-net", "none",
            "-no-reboot",
            "-no-shutdown",
            "-bios", str(args.riscv64_firmware),
            "-kernel", str(args.riscv64_image),
        ],
    }


def marker_lines(output: bytes) -> list[str]:
    return [
        line.strip()
        for line in output.decode("utf-8", errors="replace").splitlines()
        if line.strip().startswith("RIBOS-R18-")
    ]


def execute(
    architecture: str,
    command: list[str],
    timeout: float,
    log_path: Path,
) -> dict[str, object]:
    started = time.monotonic()
    output = bytearray()
    timed_out = False
    forced_kill = False
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    try:
        assert process.stdout is not None
        os.set_blocking(process.stdout.fileno(), False)
        while time.monotonic() - started < timeout:
            chunk = process.stdout.read()
            if chunk:
                output += chunk
                if b"RIBOS-R18-QEMU-FAIL" in output:
                    break
                if MARKERS[-1].encode("utf-8") in output:
                    break
            if process.poll() is not None:
                break
            time.sleep(0.02)
        else:
            timed_out = True
    finally:
        if process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                forced_kill = True
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait(timeout=2)
        assert process.stdout is not None
        tail = process.stdout.read()
        if tail:
            output += tail

    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_bytes(output)
    lines = marker_lines(bytes(output))
    counts = {marker: lines.count(marker) for marker in MARKERS}
    decoded = output.decode("utf-8", errors="replace")
    forbidden = [marker for marker in FORBIDDEN_MARKERS if marker in decoded]
    passed = (
        not timed_out
        and not forced_kill
        and not forbidden
        and lines == list(MARKERS)
        and all(count == 1 for count in counts.values())
    )
    return {
        "architecture": architecture,
        "command": command,
        "qemu_version": qemu_version(command[0]),
        "network_arguments": ["-net", "none"],
        "markers": lines,
        "marker_counts": counts,
        "semantic_receipt": CORE_RECEIPT if CORE_RECEIPT in lines else None,
        "forbidden_markers": forbidden,
        "timed_out": timed_out,
        "forced_kill": forced_kill,
        "cleanup_complete": process.poll() is not None,
        "elapsed_seconds": round(time.monotonic() - started, 3),
        "serial_log": str(log_path),
        "serial_sha256": sha256_file(log_path),
        "outcome": "passed" if passed else "failed",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu-x86-64", required=True)
    parser.add_argument("--qemu-aarch64", required=True)
    parser.add_argument("--qemu-riscv64", required=True)
    parser.add_argument("--x86-64-esp", type=Path, required=True)
    parser.add_argument("--x86-64-firmware", type=Path, required=True)
    parser.add_argument("--aarch64-image", type=Path, required=True)
    parser.add_argument("--riscv64-image", type=Path, required=True)
    parser.add_argument("--riscv64-firmware", type=Path, required=True)
    parser.add_argument("--artifact", type=Path, required=True)
    parser.add_argument("--trial-artifact", type=Path, required=True)
    parser.add_argument("--product-manifest", type=Path, required=True)
    parser.add_argument("--graph", type=Path, action="append", required=True)
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=20.0)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    if len(args.graph) != 3:
        raise SystemExit("exactly three --graph arguments are required")
    artifact_hash = sha256_file(args.artifact)
    trial_artifact_hash = sha256_file(args.trial_artifact)
    product_manifest_hash = sha256_file(args.product_manifest)
    product_manifest = json.loads(
        args.product_manifest.read_text(encoding="utf-8")
    )
    graph_reports = [
        json.loads(path.read_text(encoding="utf-8")) for path in args.graph
    ]
    graph_identity_ok = all(
        graph.get("product_id") == "ribos-qemu-validation"
        and graph.get("source_manifest_sha256") == product_manifest_hash
        and graph.get("signature_provider", {}).get("class") == "production"
        and graph.get("ribos_policy", {}).get("authorization", {}).get("class")
            == "signed-policy"
        and graph.get("protected_state_provider", {}).get("class")
            == "reference"
        and graph.get("key_policy", {}).get("keys", [{}])[0].get("id")
            == "ribon-validation-policy-key"
        and graph.get("key_policy", {}).get("keys", [{}])[0].get(
            "minimum_sequence"
        ) == 18
        and graph.get("key_policy", {}).get("keys", [{}])[0].get(
            "maximum_sequence"
        ) == 19
        for graph in graph_reports
    )
    graph_key_policy_digests = {
        graph.get("key_policy_digest_sha256") for graph in graph_reports
    }
    graph_domain_digests = {
        tuple(graph.get("protected_state_domain_digests_sha256", []))
        for graph in graph_reports
    }
    stable_bytes = args.artifact.read_bytes()
    if len(stable_bytes) < 128:
        raise SystemExit("stable artifact is shorter than the envelope")
    payload_offset = int.from_bytes(stable_bytes[24:32], "little")
    schema_offset = payload_offset + 96
    if schema_offset > len(stable_bytes) or len(stable_bytes) - schema_offset < 32:
        raise SystemExit("stable artifact schema digest is out of range")
    schema_digest = stable_bytes[schema_offset:schema_offset + 32].hex()
    image_hashes_before = {
        "amd64_esp_sha256": sha256_tree(args.x86_64_esp),
        "aarch64_sha256": sha256_file(args.aarch64_image),
        "riscv64_sha256": sha256_file(args.riscv64_image),
    }
    target_commands = commands(args)
    results = []
    for architecture in ("amd64", "aarch64", "riscv64"):
        results.append(
            execute(
                architecture,
                target_commands[architecture],
                args.timeout,
                args.output_dir / f"ribos-r18-{architecture}.log",
            )
        )

    receipts = {result["semantic_receipt"] for result in results}
    marker_sequences = {
        tuple(result["markers"]) for result in results
    }
    artifact_hash_after = sha256_file(args.artifact)
    trial_artifact_hash_after = sha256_file(args.trial_artifact)
    image_hashes_after = {
        "amd64_esp_sha256": sha256_tree(args.x86_64_esp),
        "aarch64_sha256": sha256_file(args.aarch64_image),
        "riscv64_sha256": sha256_file(args.riscv64_image),
    }
    report = {
        "schema": "ribon-ribos-cross-architecture-qemu-v2",
        "schema_version": 2,
        "source_revision": args.source_revision,
        "artifacts": {
            "confirmed": {
                "path": str(args.artifact),
                "sequence": 18,
                "sha256": artifact_hash,
                "sha256_after_run": artifact_hash_after,
                "immutable": artifact_hash == artifact_hash_after,
            },
            "trial": {
                "path": str(args.trial_artifact),
                "sequence": 19,
                "sha256": trial_artifact_hash,
                "sha256_after_run": trial_artifact_hash_after,
                "immutable": trial_artifact_hash == trial_artifact_hash_after,
            },
        },
        "targets": results,
        "composed_images": {
            "before": image_hashes_before,
            "after": image_hashes_after,
            "immutable": image_hashes_before == image_hashes_after,
        },
        "release_identity": {
            "product_id": product_manifest.get("product_id"),
            "product_manifest_path": str(args.product_manifest),
            "product_manifest_sha256": product_manifest_hash,
            "schema_digest_sha256": schema_digest,
            "graph_paths": [str(path) for path in args.graph],
            "graph_sha256": [sha256_file(path) for path in args.graph],
            "graph_identity_closed": graph_identity_ok,
            "same_key_policy_digest": len(graph_key_policy_digests) == 1,
            "key_policy_digest_sha256": (
                next(iter(graph_key_policy_digests))
                if len(graph_key_policy_digests) == 1 else None
            ),
            "same_rollback_domain_digest": len(graph_domain_digests) == 1,
            "rollback_domain_digests_sha256": (
                list(next(iter(graph_domain_digests)))
                if len(graph_domain_digests) == 1 else None
            ),
        },
        "semantic_equivalence": {
            "same_marker_sequence": len(marker_sequences) == 1,
            "same_receipt": len(receipts) == 1,
            "receipt": next(iter(receipts)) if len(receipts) == 1 else None,
        },
        "networking": {
            "normal_mode_transport": "absent",
            "qemu_nic": "disabled",
        },
        "target_scenarios": {
            "positive": ["confirmed-policy", "trial-confirm", "trial-fallback"],
            "negative": [
                "signature-mutation",
                "payload-mutation",
                "artifact-truncation",
                "wrong-product",
                "wrong-schema",
                "wrong-key",
                "wrong-sequence",
                "corrupt-protected-state",
                "instruction-budget",
                "deadline",
            ],
        },
        "evidence": {
            "host": "golden artifact and build orchestration",
            "target_object": "freestanding cross-compiled images",
            "qemu": "guest-executed policy, commit and fallback receipts",
            "hardware": "not run",
        },
    }
    passed = (
        artifact_hash == artifact_hash_after
        and trial_artifact_hash == trial_artifact_hash_after
        and image_hashes_before == image_hashes_after
        and all(result["outcome"] == "passed" for result in results)
        and len(marker_sequences) == 1
        and len(receipts) == 1
        and graph_identity_ok
        and len(graph_key_policy_digests) == 1
        and len(graph_domain_digests) == 1
    )
    report["outcome"] = "passed" if passed else "failed"
    result_path = args.output_dir / "ribos-r18-cross-architecture.json"
    result_path.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    if not passed:
        for result in results:
            if result["outcome"] != "passed":
                print(f"{result['architecture']}: {result['markers']}")
        print("RIBOS-R18-CROSS-ARCH-QEMU-FAIL")
        return 1
    print(
        "RIBOS-R18-CROSS-ARCH-QEMU-OK "
        f"confirmed={artifact_hash} trial={trial_artifact_hash} "
        "semantic-receipt=equivalent negative=10 hardware=not-run"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
