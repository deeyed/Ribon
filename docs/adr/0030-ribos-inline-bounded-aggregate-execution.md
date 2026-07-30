---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/ir/include/ribos/ir/analysis.h
  - language/ribos/ir/src/analysis.c
  - language/ribos/artifact/include/ribos/artifact/format.h
  - language/ribos/vm/include/ribos/vm/interpreter.h
  - language/ribos/vm/src/verifier.c
  - language/ribos/vm/src/runtime/interpreter.c
  - language/ribos/vm/src/runtime/storage.c
  - docs/contracts/language/ribos-bounded-aggregate-runtime-v1.md
tests:
  - make check-ribos-vm-aggregates
  - make check-ribos-vm-calls
  - make check-ribos-artifact
  - make check-ribos-verifier
  - make check
  - make docs
hardware:
  - none
supersedes:
  - aggregate bytecode accepted by the verifier but unavailable to the interpreter
---

# ADR 0030: Aggregate는 canonical fixed-capacity inline value로 실행

## Context

Ribos source와 bytecode는 bounded List/Dict, struct, Option/Result와 user enum을
표현한다. Verifier가 layout과 capacity를 확인해도 runtime이 이를 host container,
native C ABI 또는 allocator object로 다시 해석하면 다음 문제가 생긴다.

- 같은 artifact의 byte representation이 architecture나 compiler ABI에 따라 달라진다.
- heap failure와 allocator fragmentation이 verifier resource closure 밖에 생긴다.
- Dict lookup과 construction의 worst-case 비용을 설명하기 어렵다.
- padding과 unused capacity가 execution hash나 handoff output에 비결정성을 만든다.
- aggregate direct call이 scalar calling convention과 다른 숨은 stack 비용을 만든다.

Pre-OS runtime에서는 평균 lookup 성능보다 고정 capacity, 설명 가능한 최악 비용과
architecture-neutral representation이 우선한다.

## Decision

- 모든 aggregate는 verified slot 안의 fixed-capacity inline value다.
- Runtime은 heap, native union과 host structure cast를 사용하지 않는다.
- List와 Dict는 `u32` length/count header와 aligned inline payload를 가진다.
- Dict/FrozenMap은 stable key order의 fixed entry array와 bounded linear search를
  사용한다.
- Duplicate Dict key는 deterministic overwrite가 아니라 fail-closed fault다.
- Struct는 declaration ordinal, enum은 explicit one-byte tag와 aligned payload를
  사용한다.
- Aggregate construction은 slot 전체를 먼저 zero해 padding과 unused capacity를
  canonicalize한다.
- Aggregate move와 direct-call parameter/return은 exact full-slot copy다.
- 모든 function frame 크기는 8-byte 배수이고 stack resource closure는 이 padding을
  포함한다.
- Verifier는 user enum tag가 one-byte representation 범위인지 독립적으로 확인한다.
- Capacity, length, stride, tag, shape와 bounds 불일치는 catch 불가능한 VM fault다.
- Architecture, board, OS와 product-specific aggregate fast path를 generic VM에
  추가하지 않는다.

## Consequences

- 같은 artifact와 operand는 architecture에 관계없이 같은 aggregate bytes를 만든다.
- Allocation failure 없이 aggregate arena 상한이 verifier closure에 포함된다.
- Dict construction은 bounded insertion sort, lookup은 bounded linear search이므로
  최악 실행 시간을 capacity로 설명할 수 있다.
- Full-slot equality와 copy는 padding garbage에 영향을 받지 않는다.
- Nested aggregate와 aggregate direct call도 별도 native stack이나 hidden heap을
  만들지 않는다.
- Frame padding을 closure에 포함하므로 이전에 4-byte-aligned frame 뒤 nested call이
  runtime에서만 실패하던 불일치를 제거한다.
- 큰 capacity에서는 hash table보다 느릴 수 있다. Capacity 상한이 커져 linear
  lookup이 부적절해지면 ISA/layout version을 올린 별도 representation으로 다룬다.
- Affine/linear opaque handle의 runtime ownership과 helper execution은 이 결정이
  닫지 않는다.

## 기각한 대안

### Native C struct와 union

Padding, enum width와 union ABI가 target compiler에 의존해 artifact의 portable
layout 권위를 깨므로 기각한다.

### Heap-backed List와 hash Dict

Allocation 실패, resize와 hash seed가 resource closure 및 deterministic execution
설명을 복잡하게 하므로 기각한다.

### Duplicate key의 last-wins

Source lowering 순서가 의미에 개입하고 mutation된 artifact의 모호한 값을 조용히
수용하므로 기각한다.

### Aggregate별 calling convention

Scalar와 aggregate의 frame accounting을 분리해 verifier와 runtime 사이에 추가 ABI를
만드므로 기각한다. Exact slot transfer 하나로 충분하다.
