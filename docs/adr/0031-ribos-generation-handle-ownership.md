---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/vm/include/ribos/vm/handles.h
  - language/ribos/vm/include/ribos/vm/prepared.h
  - language/ribos/vm/include/ribos/vm/interpreter.h
  - language/ribos/vm/src/prepared.c
  - language/ribos/vm/src/runtime/handles.c
  - language/ribos/vm/src/runtime/interpreter.c
  - language/ribos/vm/src/verifier.c
  - docs/contracts/language/ribos-generation-handles-v1.md
tests:
  - make check-ribos-vm-handles
  - make check-ribos-vm-aggregates
  - make check-ribos-verifier
  - make check
  - make docs
hardware:
  - none
supersedes:
  - opaque handle bytes without a runtime generation and ownership authority
---

# ADR 0031: Opaque object는 generation handle과 whole-value ownership으로 실행

## Context

Stage-2 verifier는 opaque value의 schema provenance, affine/linear 사용과 helper
typestate를 정적으로 검사한다. 그러나 slot에 native pointer를 직접 넣거나 index만
저장하면 다음 런타임 공격을 막을 수 없다.

- revoke 뒤 재사용된 table index를 old value가 다시 가리킨다.
- 다른 opaque type의 token을 같은 크기라는 이유로 helper에 전달한다.
- consume callback 실패 뒤 source authority가 부활한다.
- aggregate 안 non-copy 값이 exact-copy된 뒤 source aggregate도 계속 읽힌다.
- fault cleanup이 unbounded destructor나 암묵적인 hardware rollback을 수행한다.

정적 verifier와 runtime이 서로 다른 ownership 분류를 계산해서도 안 된다.
특히 payload가 없는 `Option[Image].None`도 type 전체는 affine이므로 token byte를
스캔하는 방식으로는 올바른 소유권을 알 수 없다.

## Decision

- Ribos-visible opaque value는 canonical little-endian
  `(u32 index, u32 generation)` 8-byte token이다.
- Trusted object pointer와 drop callback은 embedder-owned process-local table에만
  있고 arena와 artifact에 직렬화하지 않는다.
- Arena의 handle region은 entry당 32-byte pointer-free record다.
- Create, non-copy move, consume과 revoke는 generation을 단조 증가시킨다. Generation
  wrap은 entry 재사용이 아니라 fail-closed limit다.
- Runtime type ID가 v1 typestate다. Lookup, borrow와 consume은 expected type ID와
  PreparedProgram이 봉인한 schema class/ownership을 다시 검사한다.
- Borrow는 synchronous helper-call 기간 하나에만 유효하다. Nested borrow와
  borrow 중 consume은 거부한다.
- Consume은 callback 전에 generation을 회전하고 `IN_FLIGHT`로 바꾼다. Callback
  성공 여부와 관계없이 source authority를 복원하지 않는다.
- Consume-replace는 같은 record의 새 verified opaque type만 available로 만들며
  source token은 계속 stale다.
- Fault cleanup은 fixed capacity 이하를 한 번 순회하고 entry마다 drop callback을
  최대 한 번 호출한 뒤 revoke한다. Callback 실패도 authority를 남기지 않는다.
- Drop callback은 host resource release hook이지 하드웨어 transaction rollback
  증명이 아니다.
- PreparedProgram은 Stage-2가 사용한 artifact type별 ownership과 named schema
  class를 caller workspace에 복사하고 binding identity에 포함한다.
- Interpreter 1.3은 `MOVE`, aggregate construction/extraction, direct call과 nested
  return에서 non-copy source slot 전체를 `MOVED`로 바꾼다. Empty aggregate도 같은
  규칙을 따른다.
- Path-sensitive ownership join이 없는 v1은 non-copy
  `Dict.get(default=...)`를 typestate violation으로 거부한다.

## Consequences

- Stale index, forged generation, wrong type와 double consume은 runtime에서도
  fail closed한다.
- Ribos slot, artifact와 arena의 stable record에는 target pointer width나 native
  callback representation이 들어가지 않는다.
- Type ownership을 PreparedProgram에서 한 번 봉인하므로 verifier, aggregate
  interpreter와 handle runtime이 같은 분류를 소비한다.
- Fault cleanup 작업량과 callback 호출 수는 product handle cap으로 제한된다.
- Interpreter source slot poison은 duplicate language value를 막지만 nested token의
  generation 회전은 helper/handle execution을 연결하는 다음 계층에서 수행한다.
- Process-local host table에는 native pointer가 있으므로 serialization, handoff와
  migration 대상이 아니다.

## 기각한 대안

### Native pointer를 slot에 저장

Target width와 address space에 종속되고 forged artifact value가 host authority가
되므로 기각한다.

### Index-only handle

Table entry 재사용 뒤 old token이 새 object를 가리키는 ABA 문제를 막지 못하므로
기각한다.

### Callback 실패 시 consume rollback

외부 effect가 부분 실행되었는지 generic VM이 알 수 없고 동일 authority의 재사용을
허용하므로 기각한다.

### Aggregate payload에서 token 존재 여부를 스캔

`None`, error variant와 zero-capacity collection도 type상 non-copy일 수 있어
ownership 의미를 byte pattern에 종속시키므로 기각한다.
