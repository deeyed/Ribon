---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/vm/include/ribos/vm/verifier.h
  - language/ribos/vm/src/verifier.c
  - language/ribos/vm/tests/verifier_tests.py
tests:
  - make check-ribos-verifier
  - make check
  - make docs
hardware:
  - none
supersedes:
  - compiler-owned bytecode semantic trust
---

# ADR 0022: Compiler 독립 Ribos bytecode verifier

## Context

Valid source, typed Policy IR와 resource closure를 만든 compiler도 executable policy의
신뢰 기반이 될 수 없다. Compiler bug나 compromise가 type row, direct target, slot
initialization 또는 frame metadata를 일관되게 위조하면 structural reader와 payload
hash는 그 위조를 발견하지 못한다.

Verifier가 Policy IR validator나 frontend semantic table을 호출하면 같은 구현 결함을
공유한다. Heap allocation을 verifier 내부 권한으로 두면 Pre-OS product가 memory
사용량을 dispatch 전에 고정하기도 어렵다.

## Decision

Ribos VM 계층에 compiler와 독립적인 Stage-1 verifier를 둔다.

- Verifier object graph는 schema, artifact codec와 VM verifier만 포함한다.
- `.rbs`, Pegen, frontend, Policy IR와 resource analyzer를 링크하지 않는다.
- Type/storage/operator/function flag numeric 의미는 ISA/VM ABI wire registry가
  소유한다.
- Structural reader 성공 뒤 selected product schema identity를 다시 계산한다.
- Type layout, slot/frame layout, direct CFG, reachable terminal mask, definite
  initialization과 direct-call stack closure를 artifact byte에서 다시 도출한다.
- Compiler가 봉인한 frame, stack, call depth와 terminal mask는 재도출 값과 일치할
  때만 허용한다.
- Scratch storage는 artifact별 size query 뒤 caller가 제공하며 verifier는 heap을
  사용하지 않는다.
- 첫 오류에서 fail closed하고 stable status, subject, row ID를 보고한다.

Stage-1 성공은 실행 certificate가 아니다. Signature, rollback/product key policy,
정확한 instruction/helper worst-case bound와 runtime dispatch counter는 후속 gate다.

## Consequences

- Compiler와 verifier의 common-mode semantic failure가 줄어든다.
- Workspace 상한을 artifact dispatch 전에 측정하고 product arena에 예약할 수 있다.
- Fixed row와 typed slot ABI 때문에 branch/call target과 definite assignment를
  source 없이 검사할 수 있다.
- User-defined struct member는 compiler가 봉인한 declaration-order ordinal을 type
  shape와 대조한다. Source spelling은 debug constant일 뿐 dispatch authority가 아니다.
  Product fact path는 selected schema로 정확히 resolve한다.
- Reachable CFG만 terminal, initialization과 call closure에 기여한다. 보존된
  unreachable row도 구조와 opcode type은 검사하지만 실행 자원에는 포함하지 않는다.

## 기각한 대안

### Compiler certificate를 신뢰

Certificate를 만든 compiler와 같은 metadata를 비교하면 hostile bytecode에 대한
독립 증거가 되지 않으므로 기각한다.

### Policy IR validator를 artifact verifier에서 재사용

Wire decode 뒤 compiler-owned IR을 재구성하면 enum numeric meaning, normalization과
validation code를 공유한다. VM verifier는 wire table을 직접 해석한다.

### Verifier 내부 heap allocation

Pre-OS allocator 상태와 실패 시점을 verifier attack surface에 넣는다. Caller-owned
workspace query로 memory authority를 product에 남긴다.
