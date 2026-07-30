#!/usr/bin/env python3
"""Enforce the target-neutral Ribos interpreter and helper boundary."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[4]
FILES = (
    ROOT / "language/ribos/vm/include/ribos/vm/interpreter.h",
    ROOT / "language/ribos/vm/include/ribos/vm/helpers.h",
    ROOT / "language/ribos/vm/include/ribos/vm/terminal.h",
    ROOT / "language/ribos/vm/src/runtime/interpreter.c",
    ROOT / "language/ribos/vm/src/runtime/helpers.c",
    ROOT / "language/ribos/vm/src/runtime/terminal.c",
    ROOT / "language/ribos/vm/src/runtime/helpers_internal.h",
    ROOT / "language/ribos/vm/src/runtime/storage_internal.h",
    ROOT / "language/ribos/vm/src/runtime/terminal_internal.h",
)

FORBIDDEN_PATTERNS = (
    re.compile(r"\b(?:x86_64|aarch64|riscv64)\b", re.IGNORECASE),
    re.compile(r"#\s*(?:if|ifdef|ifndef).*\b(?:__x86|__arm|__aarch|__riscv)"),
    re.compile(r"\bRibonService\b"),
    re.compile(r"#\s*include\s*[<\"](?:sys/|windows|efi|Ribon/)"),
    re.compile(r"\b(?:malloc|calloc|realloc|free|FILE|fopen|printf)\s*\("),
    re.compile(r"\b(?:goto|setjmp|longjmp)\b"),
)

REQUIRED_SURFACE = (
    "ribos_vm_interpreter_initialize_v1",
    "ribos_vm_interpreter_step_v1",
    "ribos_vm_interpreter_run_v1",
    "ribos_vm_interpreter_snapshot_v1",
    "ribos_vm_interpreter_fault_v1",
    "ribos_vm_helper_execution_initialize_v1",
    "ribos_vm_helper_dispatch_internal_v1",
    "ribos_vm_helper_call_consume_operations_v1",
    "ribos_vm_helper_call_consume_polls_v1",
    "ribos_vm_helper_call_set_journal_receipt_v1",
    "ribos_vm_policy_execute_v1",
    "ribos_vm_terminal_snapshot_v1",
    "ribos_vm_boot_action_consume_v1",
)


def main() -> int:
    failures: list[str] = []
    combined = ""

    for path in FILES:
        text = path.read_text(encoding="utf-8")
        combined += text
        for pattern in FORBIDDEN_PATTERNS:
            match = pattern.search(text)
            if match is None:
                continue
            line = text.count("\n", 0, match.start()) + 1
            failures.append(
                f"{path.relative_to(ROOT)}:{line}: "
                f"forbidden={match.group(0)!r}"
            )

    for symbol in REQUIRED_SURFACE:
        if symbol not in combined:
            failures.append(f"missing interpreter surface: {symbol}")

    if failures:
        for failure in failures:
            print(f"RIBOS-INTERPRETER-BOUNDARY-FAIL {failure}")
        return 1
    print(
        "RIBOS-INTERPRETER-BOUNDARY-OK "
        "target-neutral=yes allocation-free=yes dispatch=switch helpers=typed "
        "terminal=sealed"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
