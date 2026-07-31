#!/usr/bin/env python3
"""Audit protected-state product closure and Ribos authority isolation."""

from __future__ import annotations

import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile


ROOT = Path(__file__).resolve().parents[2]
EXPECTED_PROVIDER = {
    "class": "reference",
    "id": "security.protected-state.reference.ribos-qemu-validation",
    "rollback_domains": ["ribon.policy.ribos-qemu-validation.v1"],
    "symbol": "ribon_validation_protected_state_provider_descriptor",
}
REQUIRED_MAP_SYMBOLS = (
    "ribon_generated_protected_state_binding",
    "ribon_validation_protected_state_provider_descriptor",
)


def load_generator():
    """Product composer를 단일 manifest validation authority로 연다."""

    path = ROOT / "tools" / "generate_plugin_registry.py"
    spec = importlib.util.spec_from_file_location("ribon_protected_state_generator", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load product graph generator")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def expect_rejected(generator, manifest: dict[str, object], label: str) -> None:
    """In-memory manifest mutation 하나가 fail closed인지 검사한다."""

    try:
        with tempfile.TemporaryDirectory(prefix="ribon-protected-state-") as directory:
            path = Path(directory) / "product.json"
            path.write_text(json.dumps(manifest), encoding="utf-8")
            generator.load_manifest(path, "x86_64")
    except (ValueError, TypeError):
        return
    raise RuntimeError(f"protected-state negative mutation accepted: {label}")


def require_ribos_isolation() -> None:
    """Ribos frontend/VM이 raw journal authority를 import하지 않는지 검사한다."""

    roots = [ROOT / "language" / "ribos"]
    forbidden = (
        "Ribon/security/protected_state.h",
        "RibonProtectedStateProvider",
        "ribon_protected_state_open",
        "ribon_protected_state_confirm",
    )
    for root in roots:
        for path in root.rglob("*"):
            if path.suffix not in {".c", ".h"}:
                continue
            text = path.read_text(encoding="utf-8")
            leaked = [token for token in forbidden if token in text]
            if leaked:
                raise RuntimeError(f"{path}: Ribos leaks protected-state authority {leaked}")


def main() -> int:
    """Source manifest, generated reports, maps와 negative graph를 검사한다."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--graph", type=Path, action="append", required=True)
    parser.add_argument("--map", dest="maps", type=Path, action="append", required=True)
    args = parser.parse_args()
    if len(args.graph) != 3 or len(args.maps) != 3:
        raise RuntimeError("R06 protected-state graph gate requires three targets")

    generator = load_generator()
    manifest = generator.load_manifest(args.manifest, "x86_64")
    provider = manifest.get("protected_state_provider")
    digests = manifest.get("_protected_state_domain_digests")
    expected_digest = hashlib.sha256(
        EXPECTED_PROVIDER["rollback_domains"][0].encode("utf-8")
    ).digest()
    if provider != EXPECTED_PROVIDER or digests != [expected_digest]:
        raise RuntimeError("R18 protected-state binding is not exact")
    rendered = generator.render(manifest)
    for fragment in (
        EXPECTED_PROVIDER["symbol"],
        "generated_protected_state_domains",
        "ribon_generated_protected_state_binding",
        "RIBON_PROTECTED_STATE_PROVIDER_CLASS_REFERENCE",
    ):
        if fragment not in rendered:
            raise RuntimeError(f"generated protected-state closure omits {fragment}")

    for graph_path in args.graph:
        graph = json.loads(graph_path.read_text(encoding="utf-8"))
        if graph.get("protected_state_provider") != EXPECTED_PROVIDER:
            raise RuntimeError(f"{graph_path}: protected provider report drift")
        if graph.get("protected_state_domain_digests_sha256") != [
            expected_digest.hex()
        ]:
            raise RuntimeError(f"{graph_path}: protected domain digest drift")

    for map_path in args.maps:
        text = map_path.read_text(encoding="utf-8", errors="strict")
        missing = [symbol for symbol in REQUIRED_MAP_SYMBOLS if symbol not in text]
        if missing:
            raise RuntimeError(f"{map_path}: protected-state closure missing {missing}")

    source = json.loads(args.manifest.read_text(encoding="utf-8"))
    if not isinstance(source, dict):
        raise RuntimeError("source product manifest must be an object")
    missing = copy.deepcopy(source)
    missing.pop("protected_state_provider")
    expect_rejected(generator, missing, "signed product without protected provider")
    mismatch = copy.deepcopy(source)
    mismatch["protected_state_provider"]["rollback_domains"] = ["other.domain"]
    expect_rejected(generator, mismatch, "domain mismatch")
    fixture = copy.deepcopy(source)
    fixture["protected_state_provider"]["class"] = "fixture"
    expect_rejected(generator, fixture, "fixture provider in production verifier graph")
    unknown = copy.deepcopy(source)
    unknown["protected_state_provider"]["class"] = "host-file"
    expect_rejected(generator, unknown, "unknown provider class")
    require_ribos_isolation()
    print(
        "RIBON-PROTECTED-STATE-GRAPHS-OK targets=3 provider=reference "
        "signed-product=required language-authority=absent native-adapter=owner "
        "hardware-claim=none"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
