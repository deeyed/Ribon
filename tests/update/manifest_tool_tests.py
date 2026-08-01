#!/usr/bin/env python3
"""Cross-check update manifest C/Python codecs, signatures, and hostile inputs."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


PKCS8_ED25519_SEED_PREFIX = bytes.fromhex("302e020100300506032b657004220420")
SPKI_ED25519_PREFIX = bytes.fromhex("302a300506032b6570032100")


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    """Run one bounded host command and capture stable text diagnostics."""

    return subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )


def require_success(result: subprocess.CompletedProcess[str], label: str) -> None:
    """Raise with captured output when a required command fails."""

    if result.returncode != 0:
        raise RuntimeError(f"{label}: {result.stdout}{result.stderr}")


def load_seed(path: Path) -> bytes:
    """Read one exact host-only Ed25519 test seed."""

    try:
        seed = bytes.fromhex("".join(path.read_text(encoding="ascii").split()))
    except ValueError as error:
        raise ValueError("seed fixture is not hexadecimal") from error
    if len(seed) != 32:
        raise ValueError("seed fixture must contain exactly 32 bytes")
    return seed


def openssl_sign(
    openssl: str, root: Path, seed: bytes, message: bytes
) -> tuple[bytes, bytes]:
    """Sign the canonical message with an independent OpenSSL Ed25519 path."""

    private_key = root / "private.der"
    public_key = root / "public.der"
    message_path = root / "message.bin"
    signature_path = root / "signature.bin"
    private_key.write_bytes(PKCS8_ED25519_SEED_PREFIX + seed)
    message_path.write_bytes(message)
    require_success(
        run(
            [
                openssl,
                "pkey",
                "-in",
                str(private_key),
                "-inform",
                "DER",
                "-pubout",
                "-outform",
                "DER",
                "-out",
                str(public_key),
            ]
        ),
        "OpenSSL public-key derivation",
    )
    require_success(
        run(
            [
                openssl,
                "pkeyutl",
                "-sign",
                "-rawin",
                "-inkey",
                str(private_key),
                "-keyform",
                "DER",
                "-in",
                str(message_path),
                "-out",
                str(signature_path),
            ]
        ),
        "OpenSSL Ed25519 signing",
    )
    require_success(
        run(
            [
                openssl,
                "pkeyutl",
                "-verify",
                "-rawin",
                "-pubin",
                "-inkey",
                str(public_key),
                "-keyform",
                "DER",
                "-sigfile",
                str(signature_path),
                "-in",
                str(message_path),
            ]
        ),
        "OpenSSL Ed25519 verification",
    )
    public_der = public_key.read_bytes()
    signature = signature_path.read_bytes()
    if not public_der.startswith(SPKI_ED25519_PREFIX) or len(public_der) != 44:
        raise ValueError("OpenSSL returned an unexpected public-key encoding")
    if len(signature) != 64:
        raise ValueError("OpenSSL returned an unexpected signature size")
    return public_der[len(SPKI_ED25519_PREFIX):], signature


def invoke_tool(tool: Path, *arguments: str) -> subprocess.CompletedProcess[str]:
    """Invoke the tracked host tool with the active Python interpreter."""

    return run([sys.executable, str(tool), *arguments])


def expect_rejected(result: subprocess.CompletedProcess[str], label: str) -> None:
    """Require one hostile operation to fail closed."""

    if result.returncode == 0:
        raise RuntimeError(f"{label} was accepted: {result.stdout}")


def source_with_absolute_components(source: Path) -> dict[str, object]:
    """Copy a source manifest while preserving component input identities in temp dirs."""

    document = json.loads(source.read_text(encoding="utf-8"))
    for component in document["components"]:
        component["source"] = str((source.parent / component["source"]).resolve())
    return document


def source_hostile_cases(
    tool: Path, source: Path, root: Path
) -> int:
    """Reject malformed source schemas and corrupted component inputs before encoding."""

    baseline = source_with_absolute_components(source)
    cases: list[tuple[str, dict[str, object]]] = []
    wrong_hash = copy.deepcopy(baseline)
    wrong_hash["components"][0]["expected_sha256"] = "f" * 64
    cases.append(("component-hash", wrong_hash))
    unknown_role = copy.deepcopy(baseline)
    unknown_role["components"][0]["role"] = "unknown"
    cases.append(("unknown-role", unknown_role))
    duplicate_singleton = copy.deepcopy(baseline)
    duplicate_singleton["components"][1]["role"] = "kernel"
    duplicate_singleton["components"][1]["destination_class"] = "kernel-slot"
    cases.append(("duplicate-singleton", duplicate_singleton))
    too_many = copy.deepcopy(baseline)
    too_many["components"] = [copy.deepcopy(baseline["components"][1]) for _ in range(17)]
    for index, component in enumerate(too_many["components"]):
        component["logical_id"] = f"policy.{index}"
        component["bundle_offset"] = index * 4096
    cases.append(("component-capacity", too_many))
    short_input = copy.deepcopy(baseline)
    short_path = root / "short.payload"
    short_path.write_bytes(b"short")
    short_input["components"][0]["source"] = str(short_path)
    cases.append(("short-component", short_input))
    for label, document in cases:
        path = root / f"source-{label}.json"
        output = root / f"source-{label}.bin"
        path.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        expect_rejected(
            invoke_tool(tool, "assemble", "--source", str(path), "--output", str(output)),
            label,
        )
    return len(cases)


def wire_hostile_cases(
    tool: Path, c_codec: Path, manifest: bytes, root: Path
) -> int:
    """Require independent Python and C readers to reject structural wire mutations."""

    cases: list[tuple[str, bytes]] = []
    for size in (0, 31, 255, 511, 512, len(manifest) - 1):
        cases.append((f"truncate-{size}", manifest[:size]))
    cases.append(("trailing", manifest + b"\0"))

    def mutate(label: str, offset: int, replacement: bytes) -> None:
        value = bytearray(manifest)
        value[offset:offset + len(replacement)] = replacement
        cases.append((label, bytes(value)))

    mutate("total-size", 40, struct.pack("<Q", len(manifest) + 1))
    mutate("section-type", 128, struct.pack("<I", 99))
    mutate("section-wrap", 136, struct.pack("<Q", 0xFFFFFFFFFFFFFFFF))
    mutate("unknown-role", 512 + 152, struct.pack("<H", 0xFFFF))
    mutate("unknown-flag", 512 + 158, struct.pack("<H", 0x8000))
    mutate("reserved", 512 + 164, b"\x01")
    mutate("zero-size", 512 + 136, bytes(8))
    mutate("range-wrap", 704 + 128, struct.pack("<Q", 0xFFFFFFFFFFFFFFFF))
    mutate("overlap", 704 + 128, struct.pack("<Q", 32))
    duplicate_id = bytearray(manifest)
    duplicate_id[704:736] = duplicate_id[512:544]
    cases.append(("duplicate-logical-id", bytes(duplicate_id)))
    duplicate_singleton = bytearray(manifest)
    duplicate_singleton[704 + 152:704 + 154] = struct.pack("<H", 1)
    duplicate_singleton[704 + 154:704 + 156] = struct.pack("<H", 1)
    cases.append(("duplicate-singleton", bytes(duplicate_singleton)))

    for label, data in cases:
        path = root / f"wire-{label}.bin"
        path.write_bytes(data)
        expect_rejected(
            invoke_tool(tool, "inspect", "--manifest", str(path)),
            f"Python reader {label}",
        )
        expect_rejected(run([str(c_codec), "--open", str(path)]), f"C reader {label}")
    return len(cases)


def authorization_hostile_cases(
    c_codec: Path,
    manifest: bytes,
    envelope: bytes,
    root: Path,
) -> int:
    """Reject signature and every direct signed binding mutation in the C authorizer."""

    cases: list[tuple[str, bytes, bytes]] = []
    bad_signature = bytearray(envelope)
    bad_signature[-1] ^= 1
    cases.append(("signature", manifest, bytes(bad_signature)))
    for label, offset in (("sequence", 72), ("product", 256), ("domain", 416)):
        changed = bytearray(manifest)
        changed[offset] ^= 1
        cases.append((label, bytes(changed), envelope))
    for label, manifest_bytes, envelope_bytes in cases:
        hostile_manifest = root / f"authorize-{label}.manifest"
        hostile_envelope = root / f"authorize-{label}.envelope"
        hostile_manifest.write_bytes(manifest_bytes)
        hostile_envelope.write_bytes(envelope_bytes)
        expect_rejected(
            run(
                [
                    str(c_codec),
                    "--authorize",
                    str(hostile_manifest),
                    str(hostile_envelope),
                ]
            ),
            f"authorization {label}",
        )
    return len(cases)


def main() -> int:
    """Close deterministic vectors, real Ed25519, and bounded hostile corpora."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--c-codec", type=Path, required=True)
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--vector", type=Path, required=True)
    parser.add_argument("--seed", type=Path, required=True)
    parser.add_argument("--openssl", default="openssl")
    args = parser.parse_args()
    vector = json.loads(args.vector.read_text(encoding="utf-8"))
    failures: list[str] = []
    try:
        with tempfile.TemporaryDirectory(prefix="ribon-update-manifest-") as directory:
            root = Path(directory)
            first = root / "manifest-a.bin"
            second = root / "manifest-b.bin"
            require_success(
                invoke_tool(
                    args.tool,
                    "assemble",
                    "--source",
                    str(args.source),
                    "--output",
                    str(first),
                ),
                "first manifest assembly",
            )
            require_success(
                invoke_tool(
                    args.tool,
                    "assemble",
                    "--source",
                    str(args.source),
                    "--output",
                    str(second),
                ),
                "second manifest assembly",
            )
            manifest = first.read_bytes()
            if manifest != second.read_bytes():
                raise RuntimeError("repeated manifest assembly changed bytes")
            if len(manifest) != vector["expected_manifest_bytes"] or hashlib.sha256(manifest).hexdigest() != vector["expected_manifest_sha256"]:
                raise RuntimeError("manifest size or frozen SHA-256 drifted")
            c_manifest = run([str(args.c_codec), "--dump-manifest-vector"])
            require_success(c_manifest, "C manifest vector")
            if c_manifest.stdout.strip() != manifest.hex():
                raise RuntimeError("C and Python manifest bytes differ")
            inspection = invoke_tool(
                args.tool, "inspect", "--manifest", str(first)
            )
            require_success(inspection, "manifest inspection")
            if json.loads(inspection.stdout)["rollback_sequence"] != 42:
                raise RuntimeError("inspector did not derive rollback sequence")

            message_path = root / "message.bin"
            require_success(
                invoke_tool(
                    args.tool,
                    "message",
                    "--manifest",
                    str(first),
                    "--key-id",
                    vector["key_id_utf8"],
                    "--output",
                    str(message_path),
                ),
                "signed message generation",
            )
            message = message_path.read_bytes()
            if message.hex() != vector["expected_message_hex"] or hashlib.sha256(message).hexdigest() != vector["expected_message_sha256"]:
                raise RuntimeError("canonical signed message drifted")
            c_message = run([str(args.c_codec), "--dump-message-vector"])
            require_success(c_message, "C signed-message vector")
            if c_message.stdout.strip() != message.hex():
                raise RuntimeError("C and Python signed messages differ")

            public_key, signature = openssl_sign(
                args.openssl, root, load_seed(args.seed), message
            )
            if public_key.hex() != vector["expected_public_key_hex"] or signature.hex() != vector["expected_signature_hex"]:
                raise RuntimeError("independent Ed25519 vector drifted")
            signature_path = root / "signature.bin"
            signature_path.write_bytes(signature)
            envelope_path = root / "signature.envelope"
            require_success(
                invoke_tool(
                    args.tool,
                    "envelope",
                    "--manifest",
                    str(first),
                    "--key-id",
                    vector["key_id_utf8"],
                    "--signature-file",
                    str(signature_path),
                    "--output",
                    str(envelope_path),
                ),
                "signature envelope generation",
            )
            envelope = envelope_path.read_bytes()
            if len(envelope) != vector["expected_envelope_bytes"] or hashlib.sha256(envelope).hexdigest() != vector["expected_envelope_sha256"]:
                raise RuntimeError("signature envelope size or digest drifted")
            require_success(
                invoke_tool(
                    args.tool, "inspect-envelope", "--envelope", str(envelope_path)
                ),
                "signature envelope inspection",
            )
            require_success(
                run(
                    [
                        str(args.c_codec),
                        "--authorize",
                        str(first),
                        str(envelope_path),
                    ]
                ),
                "production Ed25519 authorization",
            )
            wire_cases = wire_hostile_cases(args.tool, args.c_codec, manifest, root)
            source_cases = source_hostile_cases(args.tool, args.source, root)
            authorization_cases = authorization_hostile_cases(
                args.c_codec, manifest, envelope, root
            )
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        failures.append(str(error))
        wire_cases = 0
        source_cases = 0
        authorization_cases = 0
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(
        "RIBON-UPDATE-MANIFEST-V1-OK "
        f"cross-tool=2 wire-hostile={wire_cases} source-hostile={source_cases} "
        f"authorization-hostile={authorization_cases} signature=ed25519"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
