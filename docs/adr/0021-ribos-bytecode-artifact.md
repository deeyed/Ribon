---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/artifact/
  - language/ribos/ir/
tests:
  - make check-ribos-artifact
  - make check-ribos-resources
  - make check
  - make docs
hardware:
  - none
supersedes:
  - Policy IR bytecode artifact deferral
---

# ADR 0021: Ribos bytecode ISA와 canonical artifact

## Context

Policy IR와 resource closure만으로는 설치·서명·검증 가능한 executable policy의
wire identity가 없다. Host C structure를 저장하면 padding, endian, pointer width와
compiler ABI가 artifact 의미에 들어온다. Compiler가 만든 in-memory closure를 그대로
신뢰하면 hostile artifact verifier가 CFG와 budget을 독립적으로 재검산할 수도 없다.

## Decision

VM ABI, bytecode ISA와 signature envelope를 각각 `1.0`으로 동결한다.

- `.rba`는 128-byte envelope와 canonical executable payload를 가진다.
- 모든 integer는 explicit little-endian byte reader/writer로 기록한다.
- Section directory와 fixed-size row table은 kind 순서, 8-byte alignment와 zero
  padding을 사용한다.
- 모든 offset/length/multiply/align 계산은 overflow를 검사한다.
- ISA v1은 Policy IR typed slot을 virtual register로 직접 사용하고, 별도 operand
  stack이나 physical-register allocator를 두지 않는다.
- Instruction은 fixed 48-byte row와 별도 slot-operand table을 사용한다.
- Payload는 schema digest, capability bitmap, declared budget와 independently
  recomputable resource upper bound를 함께 봉인한다.
- Payload SHA-256가 artifact identity다.
- Optional Ed25519 envelope는 key ID와 canonical 112-byte signing message를 사용한다.
- Source map은 선택적이고 executable semantics와 분리한다.

Core reader는 allocation 없이 wire range, canonical layout와 payload hash를 검사한다.
암호학적 signature trust policy와 hostile-byte semantic verifier는 별도 계층이다.

## Consequences

- Artifact byte identity가 host compiler ABI와 무관하다.
- Reader는 unaligned access나 packed structure cast 없이 사용할 수 있다.
- Compiler와 verifier가 같은 schema identity와 resource claim을 비교할 수 있다.
- Fixed instruction row는 variable-length encoding보다 크지만 decoder, fuzzing과
  worst-case fetch 설명이 단순해진다.
- Slot liveness를 재사용하지 않아 register count와 frame이 커질 수 있지만 v1의
  type·ownership 검증은 결정론적이다.
- Valid envelope와 payload hash만으로 실행 허가를 주장할 수 없다.

## 기각한 대안

### Packed C structure를 직접 저장

Padding, alignment, endian과 unaligned access가 compiler/target ABI에 의존하므로
기각한다.

### Variable-length instruction stream

Artifact 크기는 줄 수 있지만 instruction boundary, jump target과 hostile truncation
검증을 복잡하게 하므로 v1에서는 채택하지 않는다.

### Policy IR slot과 별도 register file

Allocator correctness와 debug/source mapping을 동시에 검증해야 한다. 첫 VM ABI는
typed slot을 virtual register로 직접 사용하고, 성능 자료가 필요성을 입증할 때 새
ISA version으로 도입한다.

### Signature가 envelope 전체를 직접 포함

Signature 자기 참조와 transport metadata 결합을 피하기 위해 payload hash, length,
algorithm과 key ID hash를 domain-separated canonical message로 묶는다.
