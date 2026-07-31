---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - Makefile
  - products/validation/ribos-qemu/
  - targets/ribos-validation/
  - language/ribos/vm/tests/aggregate_ownership.rbs
  - language/ribos/vm/tests/golden/aggregate_ownership-r18.sha256
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
  - unspecified Ribos cross-target execution evidence
---

# Ribos 교차 architecture 검증 v1 계약

## 목적

이 계약은 하나의 Ribos artifact를 서로 다른 native ABI에서 실행했을 때 target-core
VM, generic Ribon adapter와 generated product binding의 의미가 같은지 검증하는
diagnostic evidence product를 정의한다. OS payload 부팅이나 production signature
검증 계약이 아니다.

## Artifact authority

입력 source는 `aggregate_ownership.rbs` 하나다. `ribosc`는 동일 입력으로 unsigned
artifact 두 개를 독립 생성해야 하며 두 byte stream이 같아야 한다.

Fixture wrapper는 다음만 바꿀 수 있다.

- signed envelope flag
- algorithm ID의 Ed25519 wire value
- 고정 key ID `ribon-r18-fixture-key`
- 64-byte deterministic fixture signature
- envelope의 key/signature offset과 total length

Payload byte와 payload SHA-256은 바꾸지 않는다. 완성된 artifact의 SHA-256은
`language/ribos/vm/tests/golden/aggregate_ownership-r18.sha256`과 같아야 한다.
세 target은 이 완성 byte stream을 그대로 embed한다.

이 fixture는 signature field의 구조와 authorization flow만 검증한다. Cryptographic
Ed25519, production key store와 rollback counter의 증거로 사용할 수 없다.

## Target matrix

| Evidence target | Native entry | 실행 환경 | VM archive |
| --- | --- | --- | --- |
| AMD64 | `efi_main` | QEMU q35 + EDK II consumer | x86_64 COFF |
| AArch64 | raw entry | QEMU virt | AArch64 ELF |
| RISC-V 64 | OpenSBI dynamic next stage | QEMU virt + OpenSBI | RISC-V 64 ELF |

각 image는 다음 source 계층을 포함한다.

```text
target entry + architecture + port
  -> generated validation product graph
  -> generic Ribon Ribos adapter
  -> architecture-neutral Ribos target core
  -> embedded identical .rba
```

Frontend, Pegen, Policy IR, host allocator/writer와 host CLI는 target image에 링크하지
않는다. Validation product에는 network transport service, network capability와
inactive-slot writer authority가 없어야 하며 QEMU NIC도 `-net none`으로 비활성화한다.

## Positive 실행

각 guest는 다음 closure를 실제로 실행해야 한다.

1. artifact envelope와 payload hash open
2. boot transaction prepare
3. generated binding과 schema/helper digest 검증
4. fixture signature authorization
5. independent bytecode verifier와 PreparedProgram
6. watchdog arm
7. helper 네 번과 opaque handle transition
8. sealed BootAction 검증과 single consume
9. persistent metadata write, storage flush와 environment quiesce

성공 receipt는 세 target 모두 다음 exact value다.

```text
receipt=v1-stage8-action21-helpers4-fallback0
```

## Negative 실행

같은 guest image 안에서 다음 fault를 순서대로 실행한다.

| Fault | 요구 결과 |
| --- | --- |
| signature 마지막 byte mutation | authorization failure와 fallback 1회 |
| artifact payload byte mutation | payload hash rejection과 fallback 1회 |
| product schema identity 변경 | authorization failure와 fallback 1회 |
| maximum instruction 1 | verifier rejection, helper 0회, fallback 1회 |
| monotonic timer deadline 초과 | VM fault, transaction 미commit, fallback 1회 |

Fault path는 action을 재사용하거나 policy를 재실행할 수 없다. Factory fallback은
external `.rba` 없이 compiled callback으로 실행한다.

## Marker graph

Harness는 다음 marker가 정확히 한 번, 이 순서로 나타날 때만 target을 성공으로
판정한다.

```text
RIBOS-R18-QEMU-ENTRY
RIBOS-R18-ARTIFACT-OPEN-OK
RIBOS-R18-TRANSACTION-PREPARED
RIBOS-R18-POLICY-EXECUTE
RIBOS-R18-SIGNED-AUTH-OK
RIBOS-R18-CORE-COMMIT-OK receipt=v1-stage8-action21-helpers4-fallback0
RIBOS-R18-SIGNATURE-FALLBACK-OK
RIBOS-R18-CORRUPT-FALLBACK-OK
RIBOS-R18-SCHEMA-FALLBACK-OK
RIBOS-R18-BUDGET-FALLBACK-OK
RIBOS-R18-DEADLINE-FALLBACK-OK
RIBOS-R18-NETWORK-ABSENT-OK
RIBOS-R18-QEMU-VALIDATION-OK
```

Unknown R18 marker, 중복, 순서 변경, `RIBOS-R18-QEMU-FAIL`, timeout 또는 강제 kill은
실패다. Harness는 성공 marker를 본 뒤 process group을 종료하고 cleanup complete를
기록한다.

## Object와 build closure

`check-ribos-cross-arch-objects`는 세 link map과 image의 undefined symbol을 독립
검사한다.

- 모든 target-core VM source가 각 architecture compiler로 compile되었는가
- host support, frontend, compiler와 Policy IR object가 없는가
- hosted allocation, file I/O와 process symbol이 없는가
- network transport와 network helper provider가 없는가
- target image가 expected entry, adapter와 VM symbols를 실제 소유하는가

AArch64 target C flag에는 `-mstrict-align`이 필요하다. Source-level byte loop만으로
compiler가 unaligned wide load/store를 만들지 않는다고 가정할 수 없다.

## Result schema와 claim 경계

`build/results/ribos-r18/ribos-r18-cross-architecture.json`은 다음을 기록한다.

- source revision
- artifact path와 실행 전후 SHA-256
- target별 QEMU command/version, marker, serial path/hash와 elapsed time
- process cleanup과 forced-kill 상태
- composed image hash
- marker sequence와 semantic receipt 동등성
- QEMU NIC와 normal-mode network authority 부재

Gate 성공은 guest CPU에서 같은 artifact, verifier, VM, helper, transaction commit과
fallback 의미가 실행되었음을 증명한다. 다음은 증명하지 않는다.

- OS entry transfer 또는 Parus/Linux/FreeBSD boot
- production cryptographic signature와 anti-rollback
- recovery/provisioning networking과 OTA flash
- UEFI firmware provider 구현
- physical AMD64, AArch64 또는 RISC-V hardware
