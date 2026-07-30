---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/vm/include/ribos/vm/runtime.h
  - language/ribos/vm/tests/runtime_contract_tests.c
  - docs/contracts/language/ribos-vm-runtime-v1.md
  - Makefile
tests:
  - make check-ribos-runtime-contract
  - make check-ribos-schema
  - make check-ribos-verifier
  - make check-ribos-host-boundary
  - make check
  - make docs
hardware:
  - none
supersedes:
  - implicit Ribos runtime callback and outcome model
---

# ADR 0025: Ribos runtime와 helper execution 계약 분리

## Context

Two-stage verifier는 type, CFG, resource, ownership과 terminal closure를 증명하지만 실제
target helper의 elapsed time, I/O, durability와 callback lifetime을 설명하지 않는다.
Product schema에 이 값을 모두 넣으면 compiler가 target operation budget을 알아야 하고
deadline 또는 driver 구현 변경만으로 source semantic identity가 바뀐다.

Interpreter 구현부터 시작하면 raw artifact 실행, native service pointer, BootAction과
실제 jump의 혼동, callback 재진입과 fault recovery ownership이 public API에 고착될 수
있다. Artifact envelope, bytecode ABI와 runtime C ABI를 하나의 version으로 취급하는
경우에도 독립 evolution과 fail-closed negotiation이 불가능하다.

## Decision

Runtime C ABI 1.0과 helper execution contract 1.0을 별도 권위로 채택한다.

- Product schema 1.1은 type, helper signature, capability, ownership, typestate와 terminal
  의미를 계속 소유한다.
- Helper execution contract는 effect, synchronous mode, product mode/phase, I/O,
  operation, poll, deadline, durability와 handle-table transition을 소유한다.
- Helper descriptor의 canonical digest는 callback address를 제외하고 stable-ID 순
  little-endian field에서 계산한다.
- PreparedProgram은 artifact hash, schema digest, helper execution digest, runtime ABI와
  effective limit을 하나의 binding identity로 봉인한다.
- Runtime public structure는 fixed-width field와 process-local opaque pointer만
  사용한다. Packed C layout을 wire로 사용하지 않는다.
- `RibosPreparedProgram`과 `RibosVmHelperCall`은 opaque incomplete type이다.
- Runtime outcome은 `BootAction`, `PolicyError`, `VmFault` 세 terminal class뿐이다.
- BootAction은 single-consume intent이고 actual boot transfer가 아니다.
- Fault receipt를 봉인한 뒤 factory recovery callback을 최대 한 번 알리며 callback은
  outcome을 바꾸지 않는다.
- Public validation은 unsupported major/minor, wrong size, nonzero reserved field와
  invalid state를 fail closed한다.

## Consequences

- Compiler와 verifier는 platform operation deadline이나 callback pointer를 알 필요가
  없다.
- Product helper 구현을 바꾸거나 tighter bound를 적용하면 semantic schema를 바꾸지
  않고 execution digest와 authorization을 갱신할 수 있다.
- Type, capability 또는 ownership 변경은 execution descriptor만 바꿔 우회할 수 없고
  schema digest 변경을 요구한다.
- Runtime object는 raw artifact가 아니라 opaque PreparedProgram만 실행한다.
- Helper callback table은 prepare storage에 복사되므로 canonical digest에 native
  address를 넣지 않고도 original table mutation과 분리할 수 있다.
- Synchronous callback의 elapsed-time과 poll bound는 instruction fuel과 별개로
  집행된다.
- ABI unit 성공은 interpreter, Ribon service integration 또는 firmware 실행 성공으로
  확대 해석할 수 없다.

## 기각한 대안

### Product schema 1.2에 execution metadata 병합

Deadline, I/O와 durability 변경이 source type ABI를 불필요하게 바꾸고 compiler가
target-specific operation contract를 소비하게 하므로 기각한다.

### Runtime ABI와 bytecode VM ABI를 같은 version으로 사용

Process-local callback shape와 executable wire opcode는 evolution 원인과 reader가
다르므로 기각한다.

### Ribon service descriptor를 VM public ABI에 직접 저장

Generic VM이 Ribon Core와 firmware lifetime에 의존하고 다른 embedder가 runtime을
재사용할 수 없으므로 기각한다.

### BootAction helper에서 직접 OS로 jump

Policy terminal 의미와 quiesce, durable attempt commit, architecture entry transfer를
합쳐 fault와 point-of-no-return을 검증할 수 없으므로 기각한다.

### Async callback과 VM continuation

Continuation lifetime, cancellation과 reentrancy가 VM v1의 bounded synchronous
execution model을 크게 확장하므로 기각한다.

### Public structure를 packed wire image로 저장

Native pointer, alignment와 compiler padding이 artifact portability와 hostile-byte
검증을 깨뜨리므로 기각한다.
