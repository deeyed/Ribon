---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-07-30
code_paths:
  - language/ribos/
  - Makefile
  - docs/adr/0018-ribos-bounded-typed-front-end.md
  - docs/canonical/language/ribos-language-model.md
  - docs/contracts/language/ribos-source-language.md
tests:
  - make check-ribos-parser-pilot
  - make check-ribos-semantics
  - make ribos-parser-regenerate-check
  - make check
  - make docs
hardware:
  - not-run
supersedes:
  - none
---

# Ribos typed front-end 구현 기록

## 구현 범위

Pegen grammar action을 syntax-only dummy reduction에서 Ribos tagged AST builder로
전환했다. AST는 declaration, statement, expression, type, pattern, decorator와
source span을 보존하고 numeric node ID를 사용한다.

Lexer token에는 half-open start/end 위치와 leading trivia range를 추가했다. Space,
physical newline과 comment를 별도 trivia record로 보존하므로 formatter와 source-map
단계가 원문을 다시 사용할 수 있다.

Host semantic front-end에 다음 검사를 추가했다.

- predeclared/product type와 user struct/enum declaration
- function signature, local scope, duplicate name와 `let mut`
- integer, bool, string, bounded list/map과 expected-type inference
- `Result` propagation과 verified-image typestate
- closed `Result`/enum match의 payload binding과 exhaustiveness
- helper parameter/result와 handoff key/value schema
- direct 및 user-function-transitive capability/effect
- branch maximum, bounded-loop multiplier와 call graph를 포함한 helper upper bound
- recursive call graph, pure-function effect와 helper-budget 거부

CLI의 `--dump-tokens`, `--dump-ast`와 `--dump-semantics`는 pointer address 없이 trivia,
token, AST, type table와 function effect/bound record를 출력한다. 동일 source의
두 dump를 byte-for-byte 비교하는 gate를 추가했다.

## Host corpus 결과

Syntax corpus는 기존 surface를 보존했다.

```text
RIBOS-PARSER-CORPUS-OK positive=6 negative=12
```

Semantic corpus는 type, mutation, collection, match, typestate, capability, pure effect,
budget와 recursion 경계를 검사했다.

```text
RIBOS-SEMANTIC-CORPUS-OK positive=3 negative=16 deterministic-dump=1
```

Positive policy fixture의 bounded `for` 안 helper는 collection capacity를 곱해
helper-call upper bound에 반영했다. User pure function 호출은 call graph에 포함되지만
capability를 추가하지 않았다.

Apple Clang 21.0.0의 AddressSanitizer와 UndefinedBehaviorSanitizer를 함께 사용한
별도 build에서도 syntax와 semantic corpus가 같은 marker로 종료했다.
AddressSanitizer는 이 host에서 leak detection을 제공하지 않아 `detect_leaks=0`으로
실행했으며 leak 증거로 사용하지 않는다.

## Generation provenance

Pegen snapshot은 다음 CPython revision에서 명시적으로 재생성했다.

```text
9ccd5bb81edde823fb5fdbd51287f4a0ddfcd149
```

Grammar와 token digest는 generation receipt에 기록했다.

```text
grammar=780b6d313a0db050961adfe4a9e798fd3451fc112b52836a65ab1003ab27030d
tokens=e165aaa43d399dfde9ba9744fac94cc836f3665ccf56777b0d9757ce963f85fc
```

Generated artifact digest는 다음과 같다.

```text
f6dd5c685ae3ecc141e213724aba6b1762ce7b59cb1f8de15366effbb52fdc2a  parser.c
a5b675aac0fbbae459ed6d1c3ca6b218f20ddd5bf945a26e605303194ca3b991  tokens.h
05a2bd48d52edbb2fd67379e90466383c5afc8d3dde75ac721d2ef229222ec65  parser.receipt.json
```

Explicit regeneration comparison은 세 generated artifact 모두 `unchanged`로
종료했다. 존재하지 않는 `RIBOS_PEGEN_ROOT`를 지정한 normal forced build도 syntax와
semantic corpus를 통과해 일반 build가 Pegen checkout을 요구하지 않음을 확인했다.

Repository aggregate와 documentation gate는 다음 marker로 종료했다.

```text
RIBON-R5-AGGREGATE-OK
Sphinx build succeeded
```

## 증거 경계

이 기록의 evidence class는 host compile과 unit/corpus다. 다음 항목은 구현하거나
실행했다고 주장하지 않는다.

- canonical formatter와 parse-format-parse equality
- Policy IR, control-flow lowering과 exact instruction budget
- bytecode emission, artifact format와 static verifier
- Ribos VM, semantic helper dispatch와 policy fault recovery
- no-heap fixed-capacity firmware source parser
- Ribon boot product linkage
- QEMU, firmware 또는 physical hardware policy execution
