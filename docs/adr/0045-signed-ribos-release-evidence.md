---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-08-01
code_paths:
  - Makefile
  - products/validation/ribos-qemu/
  - tools/ribos_cross_arch_qemu.py
  - tools/check_ribos_release_reproducibility.py
tests:
  - make check-ribos-cross-arch-qemu
  - make check-ribos-release-reproducibility
  - make check-ribos-production-policy
hardware:
  - none
supersedes:
  - ADR 0036 signature fixture boundary
---

# ADR 0045: Signed Ribos release evidence는 실제 검증 경로와 재현 가능한 산출물로 닫는다

## Context

ADR 0036은 architecture-neutral VM을 검증하기 위해 signed-envelope wire shape와 deterministic
signature fixture를 사용했다. 이후 generic adapter가 production-class Ed25519 provider, immutable
key policy와 protected-state journal을 직접 결합했다. 기존 fixture callback을 계속 사용하면 target
QEMU evidence가 실제 authorization pipeline보다 약해지고 host integration과 target integration의
의미가 달라진다.

하나의 confirmed policy만 실행하면 A/B trial의 durable attempt, confirmation과 failed-trial fallback을
target ABI에서 검증하지 못한다. 또한 한 build root의 성공만으로 generated schema, embedded artifact와
target image가 checkout-local residue에 의존하지 않는다는 결론을 낼 수 없다.

## Decision

- Diagnostic product도 generic signed-policy adapter를 사용하며 fixture authorizer를 링크하지 않는다.
- 하나의 `.rbs` source와 product manifest에서 sequence 18 confirmed artifact와 sequence 19 trial
  artifact를 offline Ed25519 signer로 만든다.
- Target image는 public validation key와 reference protected-state provider만 포함한다. Private seed와
  signing symbol은 host build input에만 존재하며 target object gate가 이를 거부한다.
- AMD64 UEFI, AArch64 raw image와 RISC-V OpenSBI guest는 같은 artifact pair, schema, key-policy와
  rollback-domain identity를 사용한다.
- 각 guest는 confirmed commit, ten fail-closed negative cases, trial confirmation과 failed-trial 뒤
  confirmed fallback을 같은 marker graph로 실행한다.
- Cross-architecture JSON v2는 artifact/image immutability, manifest/schema/key/state identity, exact
  marker와 receipt, QEMU command/version, forbidden marker와 process cleanup을 보존한다.
- Release reproducibility gate는 두 clean build root에서 13개 canonical output을 독립 생성하고 byte
  hash를 비교한다.
- `check-ribos-production-policy`가 hermetic product build, executable source corpus, hostile artifact,
  Ed25519, key policy, protected state, product integration, QEMU와 clean-root reproducibility를 한 entry로
  집계한다.

## Consequences

Cross-architecture evidence는 실제 Ed25519 authorization과 generic rollback state machine까지 실행하므로
ADR 0036의 deterministic fixture보다 강하다. 같은 source와 product schema로부터 생성한 artifact와
image가 clean build root 두 곳에서 같다는 사실도 기계적으로 검사한다.

Validation key material과 reference journal은 production 배포 authority가 아니다. 이 결정은 private-key
custody, hardware root of trust, TPM/RPMB anti-replay, hostile-media power-loss durability, real OTA writer,
OS health receipt 또는 physical hardware를 증명하지 않는다.

## 기각한 대안

### Target마다 별도 policy와 key 사용

Architecture portability가 아니라 세 개의 독립 fixture만 검증하므로 기각한다.

### Host integration만으로 trial을 검증

Target compiler, ABI, entry와 freestanding journal behavior가 빠지므로 기각한다.

### Validation image에 signer 포함

Pre-OS target이 private signing authority를 가져 trust direction을 뒤집고 attack surface를 넓히므로
기각한다.

### 한 build root의 incremental rebuild를 release evidence로 사용

Stale object와 외부 output 오염을 배제하지 못하므로 두 clean root의 canonical output 비교를 사용한다.
