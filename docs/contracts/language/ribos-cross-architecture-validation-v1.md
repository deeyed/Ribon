---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-08-01
code_paths:
  - Makefile
  - products/validation/ribos-qemu/
  - targets/ribos-validation/
  - language/ribos/vm/tests/aggregate_ownership.rbs
  - language/ribos/vm/tests/golden/aggregate_ownership-r18.sha256
  - language/ribos/vm/tests/golden/aggregate_ownership-r19.sha256
  - tools/sign_ribos_policy.py
  - tools/make_ribos_qemu_manifest.py
  - tools/ribos_cross_arch_qemu.py
  - tools/check_ribos_release_reproducibility.py
  - tools/lint/ribos_cross_arch_object_lint.py
  - tools/lint/security_provider_graph_lint.py
  - tools/lint/key_policy_graph_lint.py
tests:
  - make check-ribos-golden-artifact
  - make check-ribos-cross-arch-objects
  - make check-ribos-cross-arch-qemu
  - make check-ribos-release-reproducibility
  - make check-ribos-production-policy
  - make check-security-provider-graphs
  - make check-security-key-policy-graphs
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

이 계약은 같은 source와 product schema에서 만든 confirmed/trial Ribos artifact 쌍을 서로 다른
native ABI에서 실행했을 때 target-core VM, generic Ribon adapter, generated immutable key store,
protected-state interface와 product binding의 의미가 같은지 검증하는 diagnostic evidence product를
정의한다. OS payload 부팅, production key custody 또는 hardware anti-replay 증거가 아니다.

## Artifact authority

입력 source는 `aggregate_ownership.rbs` 하나다. `ribosc`는 동일 입력으로 unsigned artifact를
독립 생성해야 하며 byte stream이 같아야 한다. Offline signer는 같은 payload를 confirmed sequence
18과 trial sequence 19로 각각 서명한다.

Offline signer는 다음 binding으로 canonical trust message를 만든다.

- selected validation product manifest exact bytes
- artifact payload와 schema digest
- normal mode와 normal-policy usage
- 고정 validation key ID `ribon-validation-policy-key`
- rollback domain `ribon.policy.ribos-qemu-validation.v1`
- confirmed sequence 18 또는 trial sequence 19

Host-only RFC 8032 test seed와 OpenSSL Ed25519가 exact 232-byte message를 서명한다. Signer는
payload byte와 payload SHA-256을 바꾸지 않고 signed envelope의 key/signature range만 조립한다.
완성된 두 artifact의 SHA-256은 `aggregate_ownership-r18.sha256`과
`aggregate_ownership-r19.sha256` golden과 각각 같아야 한다. 세 target은 이 두 byte stream을
그대로 embed한다.

세 target은 generated graph가 선택한 immutable key-policy store로 product/mode/usage/domain,
sequence와 key identity를 승인한 뒤 strict production-class Ed25519 provider로 message를
검증한다. Protected-state provider는 reference journal의 confirmed/pending/attempt 전이를 실행한다.
Public key는 공개 RFC test vector이고 private seed는 host-only test input이므로 production key
custody, hostile-media durability 또는 hardware rollback counter의 증거로 사용할 수 없다.

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
4. product-bound key-policy authorization과 strict Ed25519 signature authorization
5. independent bytecode verifier와 PreparedProgram
6. watchdog arm
7. helper 네 번과 opaque handle transition
8. sealed BootAction 검증과 single consume
9. persistent metadata write, storage flush와 environment quiesce
10. sequence 19 trial의 attempt 선차감, confirm과 failed-trial 뒤 sequence 18 fallback

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
| artifact truncation | malformed authorization failure와 fallback 1회 |
| product identity 변경 | authorization failure와 fallback 1회 |
| product schema identity 변경 | authorization failure와 fallback 1회 |
| key ID 변경 | authorization failure와 fallback 1회 |
| sequence를 17로 변경 | rollback authorization failure와 fallback 1회 |
| protected-state journal corruption | state authorization failure와 fallback 1회 |
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
RIBOS-R18-TRUNCATION-FALLBACK-OK
RIBOS-R18-PRODUCT-FALLBACK-OK
RIBOS-R18-SCHEMA-FALLBACK-OK
RIBOS-R18-KEY-FALLBACK-OK
RIBOS-R18-SEQUENCE-FALLBACK-OK
RIBOS-R18-STATE-FALLBACK-OK
RIBOS-R18-BUDGET-FALLBACK-OK
RIBOS-R18-DEADLINE-FALLBACK-OK
RIBOS-R18-TRIAL-CONFIRM-OK
RIBOS-R18-TRIAL-ROLLBACK-OK
RIBOS-R18-NETWORK-ABSENT-OK
RIBOS-R18-QEMU-VALIDATION-OK
```

Unknown R18 marker, 중복, 순서 변경, `RIBOS-R18-QEMU-FAIL`, timeout 또는 강제 kill은
실패다. Harness는 성공 marker를 본 뒤 process group을 종료하고 cleanup complete를
기록한다.

## Object와 build closure

`check-ribos-cross-arch-objects`는 세 link map과 image의 전체 symbol 및 undefined symbol을 독립
검사한다.

- 모든 target-core VM source가 각 architecture compiler로 compile되었는가
- host support, frontend, compiler와 Policy IR object가 없는가
- hosted allocation, file I/O와 process symbol이 없는가
- network transport와 network helper provider가 없는가
- signing symbol, private seed와 fixture authorization authority가 없는가
- target image가 expected entry, adapter와 VM symbols를 실제 소유하는가

`check-security-provider-graphs`는 세 generated report, map과 final image에서 production-class
provider selection, `crypto_ed25519_check`, wrapper와 descriptor의 존재를 확인한다. Upstream
signer symbol, host signer path, fixture provider와 test private seed는 final image에 없어야 한다.

`check-security-key-policy-graphs`는 세 generated report, map과 final image에서 동일 immutable
store identity, normal-only mode/usage, exact public key와 runtime validator 존재를 확인한다.
Mutable trust-store API와 Ribos target/adapter public surface의 raw key authority는 없어야 한다.

AArch64 target C flag에는 `-mstrict-align`이 필요하다. Source-level byte loop만으로
compiler가 unaligned wide load/store를 만들지 않는다고 가정할 수 없다.

## Result schema와 claim 경계

`build/results/ribos-r18/ribos-r18-cross-architecture.json` v2는 다음을 기록한다.

- source revision
- confirmed/trial artifact path, sequence와 실행 전후 SHA-256
- manifest, schema, key policy, rollback domain과 object graph identity
- target별 QEMU command/version, marker, serial path/hash와 elapsed time
- process cleanup과 forced-kill 상태
- composed image hash
- marker sequence와 semantic receipt 동등성
- QEMU NIC와 normal-mode network authority 부재

`check-ribos-release-reproducibility`는 서로 다른 두 clean build root에서 manifest, 두 signed
artifact, generated source, 세 registry와 세 architecture image 13개를 다시 만들고 각 byte hash와
release-set hash가 같은지 검사한다. 이 gate는 source tree 밖의 toolchain과 firmware binary 자체가
재현 가능하다고 주장하지 않는다.

Gate 성공은 guest CPU에서 같은 artifact, verifier, VM, helper, transaction commit과
fallback 의미가 실행되었음을 증명한다. 다음은 증명하지 않는다.

- OS entry transfer 또는 Parus/Linux/FreeBSD boot
- production private-key custody, mutable trust-store update와 hardware-backed anti-rollback
- recovery/provisioning networking과 OTA flash
- UEFI firmware provider 구현
- physical AMD64, AArch64 또는 RISC-V hardware
