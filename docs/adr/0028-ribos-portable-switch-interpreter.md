---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/vm/include/ribos/vm/interpreter.h
  - language/ribos/vm/src/runtime/interpreter.c
  - language/ribos/vm/src/runtime/storage_internal.h
  - docs/contracts/language/ribos-scalar-interpreter-v1.md
tests:
  - make check-ribos-vm-scalar
  - make check-ribos-vm-calls
  - make check-ribos-vm-loops
  - make check-ribos-runtime-storage
  - make check-ribos-verifier
  - make check-ribos-host-boundary
  - make check
  - make docs
hardware:
  - none
supersedes:
  - implicit architecture-selected scalar dispatch
---

# ADR 0028: 첫 interpreter를 portable switch와 verified instruction ID로 제한

## Context

Ribos artifact와 verifier가 direct CFG, typed slot과 exact resource closure를
제공하더라도 runtime이 byte pointer, computed goto, native signed overflow 또는 target별
assembly로 시작하면 같은 artifact의 의미를 architecture 사이에서 설명하기 어렵다.
반대로 helper, aggregate, direct call과 terminal boot transfer를 한 번에 구현하면
scalar arithmetic failure와 external effect failure의 증거 경계가 섞인다.

Pre-OS runtime에서 scalar dispatch의 우선순위는 peak throughput보다 작은 공격 표면,
bounded step, 명시적인 fault 위치와 architecture-independent 결과다. Product helper와
board service는 VM Core가 아니라 후속 embedder 계층이 소유해야 한다.

## Decision

- 첫 interpreter는 freestanding C `switch` dispatch만 사용한다.
- PC와 branch target은 verified global instruction ID다.
- 모든 persistent execution state는 caller-owned arena control region에 저장한다.
- Opcode decode 전에 instruction fuel을 검사하고 정확히 한 번 감소시킨다.
- Context generation, type와 digest를 initialize에서 결박하고 매 step 재검사한다.
- Scalar value는 storage의 explicit little-endian accessor만 사용한다.
- Signed arithmetic, shift와 narrow width를 host C undefined behavior 없이 구현한다.
- `PARAMETER`, scalar constant, `MOVE`, checked unary/binary, direct jump/branch,
  `RETURN`과 `TRAP`만 이번 실행 경계에 포함한다.
- 지원하지 않는 유효 opcode는 성공이나 no-op가 아니라 sealed `VmFault`다.
- Incremental `RETURNED`를 public `BootAction` 또는 policy success로 승격하지 않는다.
- Architecture fast path는 portable implementation과 differential evidence가 생기기
  전까지 추가하지 않는다.

## Consequences

- x86-64, AArch64와 RISC-V가 같은 interpreter source와 value semantics를 공유한다.
- 한 step의 fuel, PC와 fault receipt를 host unit test에서 정확히 관찰할 수 있다.
- Compiler가 잘못된 metadata를 만들더라도 PreparedProgram과 independent verifier
  없이 interpreter에 도달하지 않는다.
- Scalar 정책은 helper boundary 직전까지 실행할 수 있지만 실제 boot action,
  factory recovery callback과 external effect는 아직 실행하지 않는다.
- Switch dispatch 성능은 computed goto나 JIT보다 낮을 수 있다. Pre-OS 정책 규모의
  benchmark가 필요성을 입증하기 전에는 복잡성을 추가하지 않는다.
- Internal incremental API는 full-policy execute API가 아니므로 product가 직접 boot
  authority로 연결할 수 없다.
- 후속 ADR 0029는 같은 portable switch와 verified instruction ID 위에 explicit
  direct-call frame과 latch-bound loop counter를 추가한다.

## 기각한 대안

### Computed goto

Compiler extension과 native label address에 의존하고 control-flow hardening과
architecture-neutral audit 비용을 늘리므로 첫 구현에서 기각한다.

### JIT 또는 architecture별 threaded interpreter

Executable-memory authority, cache maintenance와 target별 differential proof가
필요하며 pre-OS attack surface를 크게 넓히므로 기각한다.

### Host native integer 연산에 overflow를 맡김

Signed overflow와 일부 shift가 undefined 또는 implementation-defined이고 최적화
레벨에 따라 결과가 달라질 수 있으므로 기각한다.

### 모든 ISA v1 opcode를 한 번에 공개 execute

Helper effect, aggregate layout, call frame와 terminal action의 독립 fault closure를
동시에 검증할 수 없으므로 scalar/control-flow increment부터 닫는다.
