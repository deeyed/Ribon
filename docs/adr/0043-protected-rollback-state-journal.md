---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-08-01
code_paths:
  - include/Ribon/security/protected_state.h
  - src/security/protected_state.c
  - src/security/sha256.c
  - tools/generate_plugin_registry.py
  - qstar/schemas/product.schema.json
tests:
  - make check-security-protected-state
  - make check-security-protected-state-sanitizer
  - make check-security-protected-state-graphs
hardware:
  - none
supersedes:
  - architecture monotonic timer as rollback authority
---

# ADR 0043: Protected rollback state는 typed provider와 redundant journal이 소유한다

## Context

R05의 immutable key policy는 signed object가 승인 가능한 sequence 범위를 제한하지만, 실제로
확정된 floor와 pending trial attempt를 재부팅 사이에 보존하지 않았다. 기존 monotonic timer는
boot lifetime deadline을 측정하고 persistent metadata service는 generic attempt receipt를 쓰므로
둘 모두 security rollback counter로 사용할 수 없다.

Protected state를 특정 TPM, RPMB 또는 raw flash layout에 결합하면 generic Core와 product graph가
platform-specific storage 의미를 소유하게 된다. 반대로 host file journal을 production anti-replay로
간주하면 과거 media snapshot 전체 replay를 검출할 수 없다는 한계를 숨기게 된다.

## Decision

- Architecture monotonic timer와 protected rollback state를 별도 ABI와 authority로 유지한다.
- Provider는 exact domain digest, 두 record slot, 두 selector slot과 caller-controlled
  write/flush/readback callback만 제공한다. Raw address와 device command는 public ABI에 없다.
- Record와 selector는 explicit 128-byte little-endian codec을 사용한다. Record/selector CRC32C와
  selected record SHA-256을 runtime에서 재검산한다.
- Inactive record를 flush·readback한 뒤 inactive selector를 마지막 commit point로 쓴다.
- Open은 두 selector에서 가장 큰 valid generation을 선택하고 same-generation conflict, selected
  record corruption, unavailable과 overflow를 fail closed한다.
- State machine은 `CONFIRMED(N)`과 `TRIAL(N,N+1,attempts)`만 허용한다. Pending transfer 전에
  attempt 감소가 별도 generation으로 commit되어야 한다.
- Product graph는 signature provider, key policy와 protected provider를 all-or-nothing으로 선택하고
  key-policy domain 합집합과 provider domain 집합을 exact match한다.
- Provider class를 hardware, reference, fixture로 분리한다. R18 diagnostic product는 reference를
  선택하며 production hardware claim을 열지 않는다.
- Ribos VM과 helper schema는 journal/provider/floor mutator를 import하지 않는다.

## Consequences

Update 및 boot-policy 계층은 architecture나 OS에 독립적인 confirmed/trial/confirm/fail primitive를
사용할 수 있다. Domain namespace와 exact successor 규칙 때문에 recovery가 normal floor를 낮추는
경로가 없다. Product가 protected provider를 빠뜨리거나 domain을 다르게 선언하면 target source를
생성하기 전에 거부된다.

Reference journal은 deterministic partial-flush fault injection과 codec/state-machine 검증에 충분하다.
그러나 host memory/file, ordinary block device와 checksum만으로 hostile replay-safe 또는 physical
power-loss-safe라는 주장을 할 수 없다. TPM NV, RPMB, secure element, flash atomicity와 provisioning은
각 hardware provider 및 product evidence의 후속 작업이다.

## 기각한 대안

### Monotonic timer 값을 floor로 사용

Timer reset, frequency와 persistence semantics가 security sequence와 다르므로 기각한다.

### 기존 persistent metadata attempt record 확장

Boot transaction retry receipt와 signed-object rollback authority의 lifecycle 및 failure semantics가
다르므로 별도 typed provider를 둔다.

### 단일 record 또는 단일 selector

Partial overwrite가 이전 authority까지 잃게 하므로 record와 selector 모두 두 slot을 사용한다.

### Host file provider를 production class로 승격

전체 snapshot replay를 독립적으로 감지하지 못하므로 reference evidence로만 유지한다.

### Ribos helper에 floor 변경 노출

정책 bytecode가 trust 기반을 수정하게 되므로 native state machine만 transition을 소유한다.
