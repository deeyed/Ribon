---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-30
code_paths:
  - language/ribos/frontend/include/ribos/frontend/compiler.h
  - language/ribos/frontend/src/ast.c
  - language/ribos/frontend/src/compiler.c
  - language/ribos/frontend/src/dump.c
  - language/ribos/frontend/src/lexer.c
  - language/ribos/frontend/src/semantic.c
  - language/ribos/frontend/grammar/parser.gram
  - language/ribos/schema/
tests:
  - make check-ribos-parser-pilot
  - make check-ribos-semantics
  - make ribos-parser-regenerate-check
  - make check
  - make docs
hardware:
  - none
supersedes:
  - syntax-only Pegen reduction model
---

# ADR 0018: Ribos bounded typed front-end

## Context

Ribos Pegen parser의 최초 reduction은 문법 수용 여부와 declaration 수만 반환했다.
각 grammar production이 dummy sentinel을 만들었으므로 source span, declaration,
expression, type, pattern과 decorator 사이의 구조를 다음 compiler phase가 소비할 수
없었다.

Syntax 통과만으로는 다음 보안 경계를 만들 수 없다.

- `let`과 `let mut`의 mutation 권한
- bounded collection과 `for`의 static iteration upper bound
- `Result`와 `VerifiedImage` 같은 typestate
- policy가 선언한 capability와 reachable helper effect의 포함 관계
- helper-call budget과 recursive call graph 거부
- product handoff key와 value type의 일치

반대로 source parser와 type checker를 firmware boot product에 직접 넣으면 source
parser, Pegen runtime과 compiler heap이 Pre-OS attack surface에 포함된다.

## Decision

Host compiler front-end는 다음 phase를 순서대로 수행한다.

```text
immutable UTF-8 source
  -> lossless token/trivia model
  -> Pegen syntax recognition
  -> bounded Ribos AST reduction
  -> declaration와 name resolution
  -> type, mutation와 closed-match checking
  -> capability, effect와 source-level bound closure
  -> typed AST
```

Pegen action은 Python AST, Python object와 generic dummy sentinel을 만들지 않는다.
각 accepted production은 Ribos 전용 tagged AST node를 생성한다. Node는 stable numeric
ID, half-open source span, node kind, child relation, operator와 inferred type ID를
가진다.

Lexer는 parser token과 별도로 space, physical newline과 comment trivia를 보존한다.
Token은 source byte start/end, 1-based line/column, leading-trivia range를 가진다.
Comment는 실행 의미를 갖지 않지만 formatter와 diagnostic source mapping이 원문을
복원할 수 있어야 한다.

Host parser arena는 heap-backed이지만 다음 hard limit 안에서만 동작한다.

```text
source bytes        <= 1 MiB
tokens              <= 65,536
trivia records      <= 131,072
parser depth        <= 512
AST reductions      <= 65,536
parser arena bytes  <= 16 MiB
transient gather     <= 8 MiB live bytes
semantic types      <= 256
functions           <= 64
parameters/function <= 16
struct fields       <= 64
enum variants       <= 63
payloads/variant    <= 64
locals/function     <= 256
scope depth         <= 64
```

한도를 넘으면 heap 확장으로 우회하지 않고 bounded diagnostic으로 종료한다. 이
결정은 firmware no-heap parser가 완료되었다는 뜻이 아니다. Firmware backend는 같은
token, AST와 diagnostic 의미를 caller-owned fixed storage로 별도 구현한다.

Type checker는 scalar, declared struct/enum, product-generated named type, `Option`,
`Result`, `Array`, `List`, `FrozenMap`, `Dict`와 `StringLiteral`을 구분한다.
`Result` propagation, closed `match`, immutable reassignment, homogeneous collection,
collection capacity와 bounded iteration을 검사한다.

Helper schema는 parameter/result type, capability와 result typestate를 제공한다.
Compiler는 entry에서 reachable한 사용자 함수 graph를 닫고 다음 값을 계산한다.

```text
required capability union
syntactic helper call site count
bounded branch/loop-aware helper call upper bound
maximum non-recursive call depth
```

Recursive call graph, `@pure`에서 reachable한 effect, 누락 capability와
`helper_budget` 초과는 compile failure다. Handoff helper는 heterogeneous map이 아니라
selected schema의 key/value 관계를 검사한다.

`instruction_budget`은 source AST node 수와 같지 않다. 정확한 instruction upper
bound는 Policy IR과 bytecode lowering 뒤에 계산하고 verifier가 다시 확인한다.
Front-end는 이를 추정값으로 위조하지 않는다.

Deterministic dump는 raw pointer를 출력하지 않는다. Token/trivia, AST node ID,
source span, type table, function effect와 bound만 출력하며 같은 source와 schema는
byte-for-byte 같은 dump를 생성한다.

## Consequences

- Pegen grammar가 syntax와 AST reduction을 한 변경 단위로 검증한다.
- Type, mutation, typestate와 capability 오류가 VM 실행 전에 source span으로
  귀속된다.
- Loop와 user call graph를 통한 helper upper bound가 policy declaration과 대조된다.
- Host compiler가 dynamic allocation을 사용해도 그 allocation은 hard limit 안에
  있고 boot product의 runtime dependency가 아니다.
- Product-generated helper/type schema를 compiler와 artifact verifier가 함께 소비할
  serialization format은 Policy IR 단계에서 추가해야 한다.
- Formatter, Policy IR, bytecode verifier와 VM은 이 ADR의 완료 증거에 포함되지 않는다.

## 기각한 대안

### Syntax parser 뒤에서 token을 다시 handwritten parse

Pegen grammar와 두 번째 parser가 서로 다른 language를 수용할 수 있으므로 채택하지
않는다. AST는 Pegen production action에서 직접 생성한다.

### Capability를 runtime에서만 검사

Source의 reachable effect를 compiler가 알고도 artifact 실행까지 오류를 늦추며,
developer diagnostic이 약해지므로 채택하지 않는다. Runtime과 verifier의 재검사는
compiler 검사를 대체하지 않고 보강한다.

### AST node 수를 VM instruction budget으로 사용

Lowering expansion, control-flow와 helper-call encoding을 반영하지 못하므로 채택하지
않는다.
