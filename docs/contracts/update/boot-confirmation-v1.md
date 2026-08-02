---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-08-02
code_paths:
  - include/Ribon/update/confirmation.h
  - src/update/confirmation.c
  - include/Ribon/protocol/confirmation.h
  - include/Ribon/protocol/protocol.h
  - include/Ribon/security/protected_state.h
  - src/security/protected_state.c
  - src/update/transaction.c
tests:
  - make check-boot-confirmation
  - qstar --file qstar.lua test --suite //tests:boot_confirmation_tests
hardware:
  - qemu-q35-uefi
  - rpi5-not-run
supersedes:
  - generation-and-nonce-only confirmation placeholder
---

# OS-neutral authenticated boot confirmation v1 계약

## 목적과 권한 분리

이 계약은 pending update를 실제 boot attempt 하나에 결속하고, 선택된 Boot Protocol이 승인한
authenticated health receipt만 confirmed slot으로 승격한다. 역할은 다음처럼 분리한다.

| 계층 | 소유하는 것 | 소유하지 않는 것 |
| --- | --- | --- |
| OS companion | protocol-owned health payload 생성 | slot·key·journal mutation |
| Boot Protocol | health payload의 OS별 의미 검증 | signature·freshness·commit |
| Generic confirmation engine | envelope, exact identity, signature, freshness와 commit 순서 | OS 상태·서비스 이름 |
| Protected state | attempt binding, remaining attempts, monotonic sequence floor | update slot bytes |
| Update transaction | exact pending identity와 active-slot commit | OS health 의미 |

Parus, Linux, FreeBSD, Zircon package는 companion producer 계약과 실행 evidence가 생기기 전까지
health callback을 `UNSUPPORTED`로 반환한다. Validation protocol의 8-byte receipt는 D06 실행 fixture일
뿐 일반 OS wire ABI가 아니다.

## Canonical envelope

Envelope는 native C struct가 아닌 explicit little-endian byte writer/reader로 직렬화한다. 최대 크기는
2048 byte, fixed header는 256 byte, Ed25519 signature는 마지막 64 byte다. 모든 offset/length 합은
overflow 검사 후 다음 canonical contiguous order와 exact match해야 한다.

```text
header[256]
product_id[1..128]
protocol_id[1..64]
key_id[1..64]
health_payload[1..1024]
signature[64]
```

Header는 다음 값을 인증한다.

- magic, ABI 1, exact total/header size와 zero flags/reserved
- slot ID, protocol major/minor와 policy version
- image generation, manifest sequence와 attempt sequence
- 각 variable field의 exact size/offset
- manifest SHA-256, 32-byte nonzero nonce와 health payload SHA-256

Signature message는 byte 0부터 signature offset 직전까지의 exact canonical bytes다. Product ID,
protocol ID, key ID와 health bytes도 따라서 모두 서명된다. Parser는 unknown flag, noncanonical gap,
trailing byte, zero identity/digest, oversized payload와 arithmetic overflow를 거부한다.

## Boot-attempt identity와 시작

Product graph와 pending metadata가 다음 tuple을 제공한다.

```text
mode
product_id + product_manifest_digest
protocol_id + protocol_version
policy_version
slot_id + image_generation
manifest_sequence + manifest_digest
rollback_domain_digest
```

`begin_attempt()`은 update transaction이 exact `PENDING`이며 configured maximum attempt와 일치하는지
먼저 확인한다. Product-selected nonce source에서 nonzero 32-byte nonce를 받고 protected journal의
attempt sequence를 증가시킨 뒤, tuple+nonce+attempt sequence의 SHA-256 binding을 commit한다. 열린
trial이면 더 새로운 attempt로 rebind한다. 마지막으로 payload transfer 전에 attempt를 durable하게
선차감한다. Caller가 성공 receipt를 받기 전에는 candidate로 제어를 넘기지 않는다.

Nonce 품질은 provider/product evidence의 claim이다. Generic engine은 nonzero와 exact length만
검사하며 reference fixture nonce를 production entropy로 주장하지 않는다.

## Confirmation acceptance

Acceptance 순서는 다음과 같다.

1. canonical envelope bounds, offsets와 health digest를 독립 검증한다.
2. product/protocol/version, slot/image/manifest/policy identity와 binding을 exact 비교한다.
3. update journal이 exact pending이거나 동일 identity로 이미 confirmed인지 확인한다.
4. protected journal의 current attempt sequence와 binding을 비교한다.
5. immutable key policy에서 mode, product/domain, sequence와 dedicated
   `BOOT_CONFIRMATION` usage를 승인하고 Ed25519 signature를 검증한다.
6. 선택된 Boot Protocol callback이 authenticated health payload 의미를 승인한다.
7. protected floor를 bound-confirm하고 update pending을 confirmed active slot으로 commit한다.

Protocol callback은 6단계에서 순수 validator다. Raw journal, key store, slot writer와 mutable
environment를 받지 않는다. 이전 단계 실패와 callback 거부는 durable state를 바꾸지 않는다.

Protected commit 뒤 reset되고 update commit 전이면 동일 envelope 재전달이 남은 update commit을
완료할 수 있다. 두 journal이 이미 exact confirmed이면 duplicate receipt를 반환하되 generation을
늘리지 않는다. 다른 nonce/attempt/slot/image/manifest/product/protocol receipt는 stale 또는 identity
failure다.

## Failure와 retry

- Malformed, bad signature, wrong usage/key, unhealthy payload는 pending state를 승격하지 않는다.
- Timeout/reset 뒤 새 transfer는 더 큰 attempt sequence와 새 nonce/binding을 먼저 commit한다.
- 이전 receipt는 새 binding과 일치하지 않으므로 replay된다.
- Attempt가 0이면 rebind/transfer하지 않고 confirmed predecessor 또는 recovery policy로 돌아간다.
- Unknown protocol health 의미는 fail-closed다.

Generic v1은 transaction commit 중 ambiguous provider failure가 발생하면 caller가 두 journal을 다시
열어 exact state를 판정하도록 요구한다. Reference QEMU provider는 software replay resistance나 실제
power-loss durability를 주장하지 않는다.

## Evidence와 비주장

Host corpus는 malformed/corrupt auth, wrong slot, unhealthy payload, timeout 뒤 stale receipt,
successful confirmation과 exact duplicate를 실행한다. Cross compile은 같은 engine을 x86_64,
AArch64, RISC-V freestanding object로 만든다. QEMU q35 UEFI는 동일 disk를 세 번 재사용해 pending
attempt, confirmation, confirmed duplicate reopen을 증명한다.

이 증거가 여는 claim은 다음까지다.

> Ribon generic update path는 protocol-owned authenticated health receipt를 exact boot attempt와 두
> reference journal에 결속하고 stale replay를 거부하며 duplicate confirmation을 idempotent하게
> 처리한다.

실제 OS payload가 새 slot에서 실행되었다는 주장, Parus companion producer, RPi5 live hardware,
production entropy, hardware anti-replay, physical power-loss durability, UEFI Secure Boot와 production
key provisioning은 포함하지 않는다.
