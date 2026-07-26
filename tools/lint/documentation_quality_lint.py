#!/usr/bin/env python3
"""Audit Ribon document authority, state wording, and Doxygen coverage."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

DOC_STATE_ROOTS = (
    Path("docs/canonical"),
    Path("docs/contracts"),
    Path("docs/policy"),
)

DOC_FRONT_MATTER_ROOTS = (
    Path("docs"),
)

SELF_REFERENCE_DOCS = {
    Path("docs/contracts/documentation/documentation-quality-gate.md"),
    Path("docs/policy/documentation-policy.md"),
}

REQUIRED_FRONT_MATTER = (
    "doc_type",
    "status",
    "authority",
    "last_verified",
    "code_paths",
    "tests",
    "hardware",
    "supersedes",
)

HARD_FORBIDDEN_PATTERNS = (
    re.compile(r"이번\s*라운드"),
    re.compile(r"다음\s*라운드"),
    re.compile(r"현재\s*라운드"),
    re.compile(r"현재\s*작업"),
    re.compile(r"임시로\s*(?:남긴다|둔다|사용한다|처리한다)"),
    re.compile(r"패치가\s*끝나면"),
    re.compile(r"\bworkaround\b", re.IGNORECASE),
    re.compile(r"\bfor now\b", re.IGNORECASE),
    re.compile(r"\bTODO\b"),
    re.compile(r"\bFIXME\b"),
)

REVIEW_WORDS = (
    "현재",
    "아직",
    "임시",
    "temporary",
    "deferred",
    "current state",
)

SOURCE_FUNCTION_DOXYGEN_BASELINE = 0
PUBLIC_HEADER_DOXYGEN_BASELINE = 0

SOURCE_ROOTS = (
    Path("src"),
)

PUBLIC_HEADER_ROOTS = (
    Path("include/Ribon"),
)

FUNCTION_DEFINITION_RE = re.compile(
    r"^(?:static\s+)?(?:RIBON_[A-Z_]+\s+)?"
    r"(?:[A-Za-z_][A-Za-z0-9_]*\s+)+"
    r"(?:\*\s*)?[A-Za-z_][A-Za-z0-9_]*\s*"
    r"\([^;{}]*\)\s*\{"
)

HEADER_DECLARATION_RE = re.compile(
    r"^(?:RIBON_[A-Z_]+\s+)?"
    r"(?:[A-Za-z_][A-Za-z0-9_]*\s+)+"
    r"(?:\*\s*)?[A-Za-z_][A-Za-z0-9_]*\s*"
    r"\([^;{}]*\)\s*;"
)


@dataclass(frozen=True)
class Finding:
    """한 개의 안정적인 문서 품질 finding을 나타낸다."""

    path: Path
    line: int
    message: str

    def format(self) -> str:
        """사람이 검토할 수 있는 고정 형식 문자열을 반환한다."""

        return f"{self.path}:{self.line}: {self.message}"


def iter_markdown_files(roots: tuple[Path, ...]) -> list[Path]:
    """선택된 문서 root 아래 Markdown 파일을 정렬해 반환한다."""

    files: set[Path] = set()
    for root in roots:
        absolute = ROOT / root
        if not absolute.exists():
            continue
        files.update(
            path.relative_to(ROOT)
            for path in absolute.rglob("*.md")
            if path.is_file()
        )
    return sorted(files)


def read_lines(path: Path) -> list[str]:
    """Repository-relative UTF-8 파일을 줄 단위로 읽는다."""

    return (ROOT / path).read_text(encoding="utf-8", errors="strict").splitlines()


def parse_front_matter(lines: list[str]) -> tuple[dict[str, int], int]:
    """MyST YAML front matter의 top-level key와 종료 줄을 반환한다."""

    if not lines or lines[0].strip() != "---":
        return {}, 1

    keys: dict[str, int] = {}
    for index, line in enumerate(lines[1:], start=2):
        if line.strip() == "---":
            return keys, index
        match = re.match(r"^([a-z_]+):", line)
        if match is not None:
            keys[match.group(1)] = index
    return {}, 1


def scan_front_matter() -> list[Finding]:
    """필수 front matter가 없거나 닫히지 않은 문서를 반환한다."""

    findings: list[Finding] = []
    for path in iter_markdown_files(DOC_FRONT_MATTER_ROOTS):
        keys, end_line = parse_front_matter(read_lines(path))
        if not keys:
            findings.append(Finding(path, end_line, "missing or unterminated front matter"))
            continue
        for key in REQUIRED_FRONT_MATTER:
            if key not in keys:
                findings.append(Finding(path, 1, f"missing front matter key: {key}"))
    return findings


def scan_hard_forbidden_state_words() -> list[Finding]:
    """정본 문서의 hard-forbidden 상태성 문장을 반환한다."""

    findings: list[Finding] = []
    for path in iter_markdown_files(DOC_STATE_ROOTS):
        if path in SELF_REFERENCE_DOCS:
            continue
        for line_number, line in enumerate(read_lines(path), start=1):
            if any(pattern.search(line) for pattern in HARD_FORBIDDEN_PATTERNS):
                findings.append(
                    Finding(path, line_number, "hard-forbidden state wording")
                )
    return findings


def scan_state_review_words() -> list[Finding]:
    """문맥 검토가 필요한 상태성 단어를 반환한다."""

    findings: list[Finding] = []
    for path in iter_markdown_files(DOC_STATE_ROOTS):
        if path in SELF_REFERENCE_DOCS:
            continue
        for line_number, line in enumerate(read_lines(path), start=1):
            lowered = line.lower()
            for word in REVIEW_WORDS:
                if word.lower() in lowered:
                    findings.append(Finding(path, line_number, f"review word: {word}"))
                    break
    return findings


def has_nearby_doxygen(lines: list[str], index: int) -> bool:
    """선언 또는 정의 바로 앞의 연속 comment가 Doxygen인지 반환한다."""

    cursor = index - 1
    while cursor >= 0 and not lines[cursor].strip():
        cursor -= 1
    if cursor < 0:
        return False

    stripped = lines[cursor].strip()
    if stripped.startswith("/**") and stripped.endswith("*/"):
        return True
    if not stripped.endswith("*/"):
        return False

    while cursor >= 0:
        stripped = lines[cursor].strip()
        if stripped.startswith("/**"):
            return True
        if stripped.startswith("/*"):
            return False
        if not (stripped.startswith("*") or stripped.endswith("*/")):
            return False
        cursor -= 1
    return False


def scan_source_function_doxygen() -> list[Finding]:
    """C 함수 정의의 Doxygen 누락 후보를 반환한다."""

    findings: list[Finding] = []
    for root in SOURCE_ROOTS:
        absolute_root = ROOT / root
        if not absolute_root.exists():
            continue
        for absolute_path in sorted(absolute_root.rglob("*.c")):
            path = absolute_path.relative_to(ROOT)
            lines = read_lines(path)
            for index, line in enumerate(lines):
                if not FUNCTION_DEFINITION_RE.match(line.strip()):
                    continue
                if not has_nearby_doxygen(lines, index):
                    findings.append(
                        Finding(path, index + 1, "missing function Doxygen")
                    )
    return findings


def scan_public_header_doxygen() -> list[Finding]:
    """공개 header 함수 선언의 Doxygen 누락 후보를 반환한다."""

    findings: list[Finding] = []
    for root in PUBLIC_HEADER_ROOTS:
        absolute_root = ROOT / root
        if not absolute_root.exists():
            continue
        for absolute_path in sorted(absolute_root.rglob("*.h")):
            path = absolute_path.relative_to(ROOT)
            lines = read_lines(path)
            for index, line in enumerate(lines):
                stripped = line.strip()
                if stripped.startswith(("#", "typedef", "struct", "enum", "union")):
                    continue
                if not HEADER_DECLARATION_RE.match(stripped):
                    continue
                if not has_nearby_doxygen(lines, index):
                    findings.append(
                        Finding(path, index + 1, "missing public header Doxygen")
                    )
    return findings


def print_findings(title: str, findings: list[Finding], limit: int) -> None:
    """Finding section을 안정적인 수량과 순서로 출력한다."""

    print(f"{title}: {len(findings)}")
    for finding in findings[:limit]:
        print(f"  {finding.format()}")
    if len(findings) > limit:
        print(f"  ... {len(findings) - limit} more")


def main(argv: list[str]) -> int:
    """Ribon documentation quality gate를 실행한다."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument(
        "--max-missing-source-doxygen",
        type=int,
        default=SOURCE_FUNCTION_DOXYGEN_BASELINE,
    )
    parser.add_argument(
        "--max-missing-public-header-doxygen",
        type=int,
        default=PUBLIC_HEADER_DOXYGEN_BASELINE,
    )
    args = parser.parse_args(argv)

    front_matter = scan_front_matter()
    hard_forbidden = scan_hard_forbidden_state_words()
    state_review = scan_state_review_words()
    source_doxygen = scan_source_function_doxygen()
    public_header_doxygen = scan_public_header_doxygen()

    detail_limit = 80 if args.verbose else 20
    print("ribon_documentation_quality_lint: summary")
    print_findings("front_matter", front_matter, detail_limit)
    print_findings("hard_forbidden_state_wording", hard_forbidden, detail_limit)
    print_findings("state_review_words", state_review, detail_limit)
    print_findings("missing_source_function_doxygen", source_doxygen, detail_limit)
    print_findings(
        "missing_public_header_doxygen", public_header_doxygen, detail_limit
    )
    print(
        "doxygen_baseline: "
        f"source<={args.max_missing_source_doxygen}, "
        f"public_header<={args.max_missing_public_header_doxygen}"
    )

    failed = (
        bool(front_matter)
        or bool(hard_forbidden)
        or len(source_doxygen) > args.max_missing_source_doxygen
        or len(public_header_doxygen) > args.max_missing_public_header_doxygen
    )
    if failed:
        print("ribon_documentation_quality_lint: failed", file=sys.stderr)
        return 1

    print("ribon_documentation_quality_lint: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
