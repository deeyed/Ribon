---
doc_type: adr
status: superseded
authority: historical
last_verified: 2026-07-30
code_paths:
  - language/ribos/
  - Makefile
tests:
  - make check-ribos-parser-snapshot
  - make check-ribos-parser-pilot
  - make ribos-parser-regenerate-check
  - make check
  - make docs
hardware:
  - none
supersedes:
  - ADR 0016 path layout
superseded_by:
  - 0019-ribos-policy-ir-and-product-schema
---

# ADR 0017: Ribos language project hierarchy

## Context

Ribos parser pilot은 `language/grammar`, `language/generated`, `language/include`,
`language/src`, `language/tools`, repository `tools/ribosc`와 `tests/language`에 나뉘어
있었다. 이 구조는 `language/` 아래에 다른 language나 schema project가 추가될 때
ownership을 모호하게 만들고, 한 Ribos 변경의 source·tool·test 범위를 여러
top-level directory에서 찾아야 하게 한다.

각 filename도 `ribos_parser.c`, `ribos_lexer.c`, `ribos_tokens.h`처럼 project 이름을
반복했다. Directory namespace와 filename namespace가 중복되면 path가 길어지고
project 내부 역할보다 전역 이름이 앞선다.

## Decision

Ribos language project의 모든 source-owned asset은 `language/ribos/` 아래에 둔다.

```text
language/ribos/
  README.md
  grammar/
  generated/
  include/ribos/
  src/
  tools/
  tests/
```

각 directory ownership은 다음과 같다.

| Directory | Ownership |
| --- | --- |
| `grammar/` | Pegen grammar와 token specification |
| `generated/` | 추적되는 C parser, token header와 generation receipt |
| `include/ribos/` | host parser public C include namespace |
| `src/` | lexer, parser adapter와 standalone host runtime |
| `tools/` | generation, staleness check와 parser CLI |
| `tests/` | positive/negative syntax corpus와 corpus runner |

Project directory가 `ribos` namespace를 제공하므로 내부 filename은 역할만 표현한다.

```text
ribos.gram                     -> grammar/parser.gram
ribos_parser.c                 -> generated/parser.c 또는 src/parser.c
ribos_parser.receipt.json      -> generated/parser.receipt.json
ribos_tokens.h                 -> generated/tokens.h
ribos_lexer.c                  -> src/lexer.c
ribos_parser_internal.h        -> src/parser_internal.h
ribos_runtime.c                -> src/runtime.c
ribos_parse.c                  -> tools/parse.c
```

Old path를 위한 forwarding file, symlink와 build alias는 두지 않는다. Root Makefile과
문서 계약은 새 path만 소비한다.

Filename hard cut은 C global namespace hard cut이 아니다. Public function, type,
diagnostic marker와 installed include path는 다른 C component와 충돌하지 않도록
`ribos_`, `RIBOS_`와 `ribos/` namespace를 유지한다.

ADR 0016의 tracked snapshot과 explicit regeneration 결정은 유지한다. 일반 build는
`language/ribos/generated/` snapshot을 직접 컴파일하고 Pegen을 실행하지 않는다.

## Consequences

- Ribos grammar, implementation, tooling과 corpus를 하나의 subtree로 검토할 수 있다.
- `language/`는 여러 language project를 수용할 수 있는 container가 된다.
- Project 내부 filename은 짧아지지만 public C symbol은 명시적 namespace를 보존한다.
- 과거 devlog와 ADR의 old path는 당시 evidence를 설명하는 historical record로
  유지한다.
- Downstream script가 old source path를 직접 사용했다면 새 path로 즉시 전환해야
  하며 compatibility 기간은 없다.

## 기각한 대안

### Tests와 generator를 top-level에 유지

Parser implementation만 이동하고 corpus와 generator를 남기면 language project가
self-contained하지 않으므로 채택하지 않는다.

### Filename prefix 유지

전역 C symbol과 달리 source filename은 directory가 이미 namespace를 제공하므로
중복 prefix를 유지하지 않는다.

### Compatibility symlink 제공

Old path가 다시 dependency로 굳고 hard cut 검증이 불가능해지므로 채택하지 않는다.
