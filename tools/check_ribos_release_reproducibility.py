#!/usr/bin/env python3
"""Build the signed Ribos validation release closure in two fresh roots."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


CANONICAL_OUTPUTS = (
    "targets/ribos-r18/generated/product.json",
    "targets/ribos-r18/generated/policy-signed.rba",
    "targets/ribos-r18/generated/policy-trial-signed.rba",
    "targets/ribos-r18/generated/embedded_policy.c",
    "targets/ribos-r18/generated/embedded_trial_policy.c",
    "targets/ribos-r18/amd64/generated/plugin_registry.c",
    "targets/ribos-r18/amd64/BOOTX64.EFI",
    "targets/ribos-r18/aarch64/generated/plugin_registry.c",
    "targets/ribos-r18/aarch64/ribon-ribos.elf",
    "targets/ribos-r18/aarch64/ribon-ribos.bin",
    "targets/ribos-r18/riscv64/generated/plugin_registry.c",
    "targets/ribos-r18/riscv64/ribon-ribos.elf",
    "targets/ribos-r18/riscv64/ribon-ribos.bin",
)


class ReproducibilityError(RuntimeError):
    """Report the first bounded release-identity divergence."""


def sha256_bytes(data: bytes) -> str:
    """Return the lowercase SHA-256 identity of caller-owned bytes."""

    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    """Read and hash one canonical output exactly once."""

    return sha256_bytes(path.read_bytes())


def run(
    command: list[str],
    root: Path,
    log: Path,
    timeout: int = 600,
) -> str:
    """Run one bounded command and append complete output to the build log."""

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
        timeout=timeout,
    )
    with log.open("a", encoding="utf-8") as stream:
        stream.write("COMMAND " + " ".join(command) + "\n")
        stream.write(completed.stdout)
        stream.write(f"RETURN {completed.returncode}\n")
    if completed.returncode != 0:
        tail = "\n".join(completed.stdout.splitlines()[-40:])
        raise ReproducibilityError(f"command failed: {command[-1]}\n{tail}")
    return completed.stdout.strip()


def snapshot(build_root: Path) -> dict[str, str]:
    """Hash the product-owned release outputs and reject missing artifacts."""

    result: dict[str, str] = {}
    for relative in CANONICAL_OUTPUTS:
        path = build_root / relative
        if not path.is_file():
            raise ReproducibilityError(f"missing canonical output: {relative}")
        result[relative] = sha256_file(path)
    return result


def main() -> int:
    """Compare two clean-root builds and persist a pointer-free result."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make", default="make")
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--work-root", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    work_root = args.work_root.resolve()
    work_root.mkdir(parents=True, exist_ok=True)
    log = work_root / "commands.log"
    log.write_text("", encoding="utf-8")

    try:
        source_revision = run(
            ["git", "rev-parse", "HEAD"], root, log, timeout=10
        )
        source_diff = subprocess.run(
            ["git", "diff", "--binary", "--", "."],
            cwd=root,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=30,
        )
        if source_diff.returncode != 0:
            raise ReproducibilityError("could not capture source snapshot identity")
        source_diff_sha256 = sha256_bytes(source_diff.stdout)
        with tempfile.TemporaryDirectory(prefix="run-", dir=work_root) as raw:
            temporary = Path(raw)
            roots = (temporary / "build-a", temporary / "build-b")
            snapshots = []
            for build_root in roots:
                run(
                    [
                        args.make,
                        "--no-print-directory",
                        f"BUILD_ROOT={build_root}",
                        "ribos-r18-release-artifacts",
                    ],
                    root,
                    log,
                )
                snapshots.append(snapshot(build_root))
            if snapshots[0] != snapshots[1]:
                changed = sorted(
                    path
                    for path in set(snapshots[0]) | set(snapshots[1])
                    if snapshots[0].get(path) != snapshots[1].get(path)
                )
                raise ReproducibilityError(
                    "independent build roots diverged: " + ", ".join(changed)
                )
            combined = hashlib.sha256()
            for path, digest in sorted(snapshots[0].items()):
                combined.update(path.encode("utf-8"))
                combined.update(b"\0")
                combined.update(bytes.fromhex(digest))
            report = {
                "schema": "ribon-ribos-release-reproducibility-v1",
                "source_revision": source_revision,
                "source_diff_sha256": source_diff_sha256,
                "independent_build_roots": 2,
                "canonical_output_count": len(CANONICAL_OUTPUTS),
                "canonical_outputs": snapshots[0],
                "release_set_sha256": combined.hexdigest(),
                "outcome": "passed",
            }
        (work_root / "result.json").write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    except (
        OSError,
        ReproducibilityError,
        subprocess.SubprocessError,
        ValueError,
    ) as error:
        print(f"RIBOS-RELEASE-REPRODUCIBILITY-FAIL: {error}", file=sys.stderr)
        return 1
    print(
        "RIBOS-RELEASE-REPRODUCIBILITY-OK roots=2 outputs="
        f"{len(CANONICAL_OUTPUTS)} release={report['release_set_sha256']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
