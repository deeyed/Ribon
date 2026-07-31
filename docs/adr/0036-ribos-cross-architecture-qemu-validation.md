---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - Makefile
  - products/validation/ribos-qemu/
  - targets/ribos-validation/
  - tools/make_ribos_signed_fixture.py
  - tools/make_ribos_qemu_manifest.py
  - tools/ribos_cross_arch_qemu.py
  - tools/lint/ribos_cross_arch_object_lint.py
tests:
  - make check-ribos-golden-artifact
  - make check-ribos-cross-arch-objects
  - make check-ribos-cross-arch-qemu
  - make check-ribos-r18
  - make check
  - make docs
hardware:
  - none
supersedes:
  - host-only Ribos target-core execution evidence
---

# ADR 0036: 동일 Ribos artifact의 교차 architecture QEMU 검증

## Context

Host replay와 host-reference product fixture는 production VM 코드, generated product
binding과 Ribon transaction을 각각 실행했다. 그러나 두 증거는 host process의
object와 ABI를 사용했다. 따라서 같은 `.rba`가 AMD64, AArch64와 RISC-V 64의
freestanding image에서 같은 verifier, runtime, helper와 terminal 의미를 유지하는지
증명하지 못했다.

Architecture별로 서로 다른 policy source나 handwritten bytecode를 사용하면 target
build만 확인할 뿐 artifact portability를 확인하지 못한다. Normal boot manager에
validation callback과 fixture key를 넣으면 production object graph와 evidence 전용
authority도 섞인다.

## Decision

- `ribos-qemu-validation`은 normal boot manager와 분리된 diagnostic executable
  product identity다.
- Host `ribosc`가 하나의 `.rbs`에서 canonical unsigned artifact를 두 번 생성하고,
  deterministic fixture envelope를 붙인 뒤 golden SHA-256과 비교한다.
- 완성된 artifact byte는 세 target image에 그대로 embed한다. Target별 recompilation,
  bytecode patch 또는 architecture conditional policy는 허용하지 않는다.
- AMD64는 EDK II 위 UEFI application, AArch64는 QEMU virt raw image, RISC-V 64는
  OpenSBI `fw_dynamic` 위 S-mode image로 실행한다.
- 세 image는 각각 cross-compiled `libribos-target-core.a`를 링크한다. Object gate는
  host support, frontend, IR, compiler와 network transport import가 없는지 map과
  undefined symbol에서 검사한다.
- Generated validation manifest가 product schema, helper contract, typed service route,
  normal-mode network 부재와 resource limit을 소유한다. Product callback은 test-only
  signature authorization과 compiled factory fallback을 제공한다.
- Guest는 signed authorization과 single BootAction consume 뒤 기존 transaction의
  metadata commit, flush와 quiesce를 실행한다. Signature mutation, payload corruption,
  schema mismatch, instruction budget와 deadline fault는 모두 external artifact 없이
  factory fallback으로 닫힌다.
- Harness는 artifact 실행 전후 hash, exact marker 순서와 수량, semantic receipt,
  QEMU command/version, serial hash와 process cleanup을 JSON으로 기록한다.
- AArch64 freestanding C는 `-mstrict-align`로 build한다. Byte-wise wire access를 compiler가
  비정렬 wide access로 합치는 것을 target ABI가 허용한다고 가정하지 않는다.

## Signature fixture 경계

R18 artifact는 Ed25519 algorithm ID, key ID와 64-byte signature field의 wire shape를
가진다. Signature byte는 deterministic fixture이며 cryptographic Ed25519 signature가
아니다. Validation product authorizer는 exact fixture key와 byte를 비교한다.

따라서 이 gate는 다음을 증명한다.

- signed-envelope authorization callback이 target에서 호출됨
- signature mutation이 authorization failure로 닫힘
- envelope, payload와 schema identity가 VM 실행 전에 결박됨

다음은 증명하지 않는다.

- Ed25519 cryptographic verification
- production root key, secure storage와 anti-rollback
- update manifest 또는 image trust chain

## Consequences

- 하나의 little-endian artifact와 같은 semantic receipt가 세 native ABI에서
  실행되므로 architecture-neutral VM 주장을 host object보다 강하게 검증한다.
- Target build flag와 object freshness가 evidence의 일부가 된다. R18 target object는
  Makefile 변경에도 재build되어 stale compiler flag를 재사용하지 않는다.
- Validation product는 normal boot target의 지원 등급을 올리지 않는다.
- Recovery/provisioning network, OTA flash, OS payload transfer와 live hardware는 별도
  product와 evidence 계약으로 남는다.

## 기각한 대안

### Architecture별 artifact 생성

Compiler와 source가 같은 것만 보이고 wire artifact portability와 동일 의미 실행을
증명하지 못하므로 기각한다.

### Host replay 결과를 cross-architecture 증거로 재사용

Target compiler, ABI, linker, entry와 freestanding memory behavior를 실행하지 않으므로
기각한다.

### Normal product에 fixture callback을 추가

Production graph에 diagnostic key와 failure injection surface가 들어가므로 별도
validation product를 사용한다.
