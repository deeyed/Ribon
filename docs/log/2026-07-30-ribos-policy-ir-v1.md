---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-07-30
code_paths:
  - language/ribos/frontend/
  - language/ribos/schema/
  - language/ribos/ir/
  - language/ribos/vm/
  - Makefile
  - docs/adr/0019-ribos-policy-ir-and-product-schema.md
  - docs/contracts/language/ribos-policy-ir-v1.md
tests:
  - make check-ribos-parser-pilot
  - make check-ribos-semantics
  - make check-ribos-schema
  - make check-ribos-ir
  - sanitizer Ribos corpus
  - make check
  - make docs
hardware:
  - not-run
supersedes:
  - none
---

# Ribos Policy IR v1 구현 기록

## Hard cut와 project hierarchy

Ribos source extension을 `.rbs`로 고정하고 active language corpus의 기존 extension을
모두 제거했다. Compatibility alias와 이중 fixture discovery는 추가하지 않았다.

기존 단일 language project를 다음 경계로 분리했다.

```text
frontend  grammar, generated parser, AST, semantic와 AST-to-IR bridge
schema    versioned product type/helper/handoff schema와 identity
ir        typed CFG module, validator와 deterministic dump
vm        후속 bytecode/verifier/runtime ownership README
```

Public include도 `ribos/frontend`, `ribos/schema`, `ribos/ir` namespace로 분리했다.
일반 build는 이동된 tracked parser snapshot을 직접 컴파일하며 Pegen을 실행하지 않는다.

## Product schema

`semantic.c`에 내장되어 있던 product type, fact/member, helper signature/capability와
handoff field table을 `language/ribos/schema/`로 분리했다.

Schema는 stable-ID-ordered canonical little-endian encoding과 SHA-256 identity를
제공한다. Reference host schema identity는 다음과 같다.

```text
da48c96b07390ecbadb6eef06ab6cdfbd
07b9a6de1bb1aa8e876e19f24378f52
```

Independent host `shasum -a 256`로 canonical encoded bytes가 같은 digest를 만드는지
확인했다. Product ID만 바꾼 schema가 다른 identity를 만들고 duplicate stable ID가
거부되는 unit test를 추가했다.

Reference schema는 최종 product authority가 아니다. Product/plugin graph가 동일한
versioned artifact를 생성해 compiler와 verifier에 전달하는 경계만 이번 라운드에
고정했다.

## Policy IR v1

Typed AST lowering은 다음 record를 생성한다.

- function-owned typed virtual slot
- explicit basic block와 direct jump/branch
- direct user-function call과 helper stable-ID call
- phi 대신 branch별 explicit move
- left-to-right argument, collection와 binary operand evaluation
- frontend enum과 분리된 checked operator ID
- `Option`, `Result`, user enum와 struct variant/aggregate instruction
- user aggregate shape table
- source-map table
- helper instruction과 1:1인 call-site table
- product schema digest

`Some`, `Err`, Option match와 payload, user enum constructor/payload와 struct named
constructor의 semantic/lowering path도 corpus에서 닫았다.

IR structural validator는 table/range, cross-function slot/block, direct target,
terminator, source map, aggregate shape/arity, sum tag, return type와 helper call-site
consistency를 검사한다. Compile failure 때 partially lowered module은 reset된다.

## Parser snapshot provenance

Hierarchy include 경로 변경으로 grammar digest가 바뀌어 pinned CPython Pegen
revision에서 snapshot을 명시적으로 재생성했다.

```text
Pegen revision
9ccd5bb81edde823fb5fdbd51287f4a0ddfcd149

grammar
d1a96f267c2b768690524f69ec9db7d2c8089d4ccc30653a0830e6117beec722

tokens
e165aaa43d399dfde9ba9744fac94cc836f3665ccf56777b0d9757ce963f85fc
```

Tracked output digest는 다음과 같다.

```text
82af16e6dedadb5dc3b339261eaeaf0a2cf2cf88bbbd07a77a55f47812f0f3d4  parser.c
7e656c47b532debb4ebba942a598af5bf598244bd4b75a8aaea5e285b8f946eb  tokens.h
014e76351008d6e337416a6f6de9bf1987c8f8f8c2cfdafd190fb5f837858c40  parser.receipt.json
```

## Host evidence

Focused host gates는 다음 marker로 종료했다.

```text
RIBOS-PARSER-CORPUS-OK positive=6 negative=12
RIBOS-SEMANTIC-CORPUS-OK positive=4 negative=17 deterministic-dump=1
RIBOS-SCHEMA-TEST-OK format=1.0 identity=da48c96b...378f52
RIBOS-IR-MODULE-TEST-OK valid=1 rejected-missing-helper-site=1
RIBOS-POLICY-IR-V1-OK fixtures=4 schema=da48c96b...378f52 opcodes=21 deterministic=1
```

Apple Clang AddressSanitizer와 UndefinedBehaviorSanitizer를 함께 사용한 별도
`BUILD_ROOT`에서도 네 gate가 같은 marker로 종료했다. 이 host의 leak detector는
사용하지 않아 `ASAN_OPTIONS=detect_leaks=0`으로 실행했으며 leak 증거로 해석하지
않는다.

Repository aggregate와 Sphinx 결과는 다음 marker로 종료했다.

```text
RIBON-R5-AGGREGATE-OK
Sphinx build succeeded
```

## 증거 경계

이 기록의 evidence class는 host compile, unit/corpus와 documentation build다. 다음
항목은 구현하거나 실행했다고 주장하지 않는다.

- serialized 또는 signed policy artifact
- bytecode emitter와 exact VM instruction budget
- adversarial artifact static verifier
- Ribos VM과 semantic helper runtime dispatch
- no-heap firmware source parser
- Ribon boot product policy linkage
- QEMU, firmware 또는 physical hardware policy execution
