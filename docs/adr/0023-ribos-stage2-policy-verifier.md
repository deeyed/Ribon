---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/schema/include/ribos/schema/schema.h
  - language/ribos/vm/include/ribos/vm/verifier.h
  - language/ribos/vm/src/verifier.c
tests:
  - make check-ribos-schema
  - make check-ribos-verifier
  - make check
  - make docs
hardware:
  - none
supersedes:
  - Stage-1-only Ribos semantic eligibility
---

# ADR 0023: Ribos Stage-2 policy verifier

## Context

Stage-1은 hostile artifact의 구조, type, CFG, initialization과 frame/stack을 독립적으로
검사하지만 helper가 가진 ownership과 terminal 의미를 알 수 없었다. Helper path와
signature만으로는 다음을 안전하게 추론할 수 없다.

- opaque handle이 copy 가능한지 affine/linear인지
- argument가 borrow인지 consume인지
- helper가 typestate transition인지
- 어떤 helper result가 최종 boot action인지
- compiler가 기록한 capability와 resource bound가 reachable bytecode와 같은지

이 의미를 helper 이름이나 reference product의 stable ID로 hardcode하면 generic
Ribon product와 plugin-generated schema 경계를 깨뜨린다.

## Decision

Product schema format 1.1과 compiler-independent Stage-2 verifier를 채택한다.

- Schema digest에 policy entry ABI, type ownership, parameter mode, typestate와 terminal
  helper flag를 포함한다.
- Stage-2는 Stage-1 성공 뒤 같은 allocation-free verifier object graph에서 실행한다.
- Reachable capability와 exact instruction/helper closure를 bytecode에서 재도출한다.
- Opaque provenance를 제한하고 CFG must-availability로 affine/linear consume을 검사한다.
- Typestate transition은 schema가 지정한 ownership-bearing input을 consume해야 한다.
- Entry policy의 모든 `Ok` return은 terminal boot action 하나, `Err`와 `TRAP`은
  terminal action 0개여야 한다.
- Terminal boot helper는 entry policy에서만 호출하고 이후 effect/control transfer를
  허용하지 않는다.

Stage-2 성공은 compiler와 verifier 사이의 좁은 semantic closure다. Signature,
rollback, installation authority와 runtime counter는 execution eligibility의 별도
gate다.

## Consequences

- Compiler metadata와 일치하도록 함께 변조한 artifact도 resource/capability
  재도출에서 거부된다.
- `VerifiedImage`, `UpdateReceipt`와 `BootAction`을 raw symbol로 위조하거나 두 번
  consume할 수 없다.
- Product가 ownership/helper 의미를 바꾸면 schema digest가 달라지고 기존 artifact는
  fail closed한다.
- Affine value는 사용하지 않을 수 있지만 linear resource destruction과 deadline은
  VM runtime 책임으로 남는다.
- Hostile corpus가 source-level misuse와 sealed artifact mutation을 함께 다룬다.

## 기각한 대안

### Helper stable ID를 verifier에 hardcode

Reference product에서만 맞고 plugin graph가 바뀌면 verifier가 별도 ABI authority가
되므로 기각한다.

### Compiler의 capability/resource certificate 승인

Compiler bug나 compromise가 certificate와 bytecode를 함께 위조할 수 있으므로
독립 재도출이 아니다.

### 모든 opaque value를 copy 금지

`Slot`처럼 identity token을 반복 borrow하는 API까지 불필요하게 제한한다. Product
schema가 copy/affine/linear를 명시하게 한다.
