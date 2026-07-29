---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-07-30
code_paths:
  - language/ribos/
  - Makefile
  - README.md
tests:
  - make check-ribos-parser-snapshot
  - make check-ribos-parser-pilot
  - make ribos-parser-regenerate-check
  - make check
  - make docs
hardware:
  - not-run
supersedes:
  - none
---

# Ribos project hierarchy hard cut 기록

## 변경 범위

- `language/grammar`, `language/generated`, `language/include`, `language/src`와
  `language/tools`를 `language/ribos/` project subtree로 통합했다.
- `tools/ribosc`의 generator와 snapshot checker, `tests/language`의 syntax corpus를
  같은 project subtree로 이동했다.
- Directory가 project namespace를 소유하도록 C, grammar와 generated filename의
  `ribos_` 반복을 제거했다.
- Public C symbol, include namespace와 diagnostic marker의 Ribos namespace는
  유지했다.
- Old path forwarding file이나 symlink는 만들지 않았다.

## 검증 결과

Legacy project path와 redundant filename gate는 다음 marker로 종료했다.

```text
RIBOS-PROJECT-LAYOUT-OK root=language/ribos
```

Pegen snapshot regeneration comparison은 parser, token header와 receipt 모두
`unchanged`로 종료했다. Generation provenance는 다음과 같다.

```text
grammar=c6793b583c96fd062f6f5c9b1585c0d826593749cb0153f2ebe9c3af01e52541
tokens=e165aaa43d399dfde9ba9744fac94cc836f3665ccf56777b0d9757ce963f85fc
pegen=9ccd5bb81edde823fb5fdbd51287f4a0ddfcd149
```

Generated artifact SHA-256은 다음과 같다.

```text
b4c4e2b2c701fe923f507525cfc97d7cf7f1b8d4d227fc1fb7bfe947109f4e1d  parser.c
4ec53e76ba6e5af6b550f6fff03dfab3e8734f288e8fe8048ea42d0fd48736ba  tokens.h
```

존재하지 않는 `RIBOS_PEGEN_ROOT`를 지정한 강제 parser rebuild에서도 normal build와
corpus가 성공했다.

```text
RIBOS-PARSER-CORPUS-OK positive=6 negative=12
RIBOS-PARSER-PILOT-OK bytes=1420 tokens=321 declarations=3 depth=60
```

Repository aggregate와 문서 gate는 다음 terminal result로 종료했다.

```text
RIBON-R5-AGGREGATE-OK
Sphinx build succeeded
```

## 증거 경계

이 기록은 source hierarchy, generated snapshot reproducibility, host parser corpus와
repository build/documentation gate에 한정된다. Typed AST, type checker, bytecode,
verifier, VM, firmware parser, QEMU 또는 physical hardware policy execution을
증명하지 않는다.
