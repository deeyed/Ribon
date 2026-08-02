---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-08-02
code_paths:
  - include/Ribon/update/confirmation.h
  - src/update/confirmation.c
  - include/Ribon/protocol/protocol.h
  - src/security/protected_state.c
  - src/update/transaction.c
tests:
  - make check-boot-confirmation
hardware:
  - qemu-q35-uefi
  - rpi5-not-run
supersedes:
  - implicit OS-specific confirmation core
---

# ADR: Generic core는 identity를, Boot Protocol은 health 의미를 소유한다

## 맥락

Pending update 승격에는 단순한 `boot succeeded` bit보다 강한 fresh identity가 필요하다. OS 이름이나
서비스 marker를 core에 넣으면 새 OS마다 update ABI가 변하고, nonce만 비교하면 다른 product,
protocol, image generation 또는 manifest의 receipt를 잘못 재사용할 수 있다. 반대로 OS health 의미를
generic engine이 판정할 수는 없다.

## 결정

Generic engine은 product/protocol/version/slot/image/manifest/policy/nonce/attempt tuple의 canonical
binding, dedicated key authorization, Ed25519 verification과 두 durable journal commit을 소유한다.
Boot Protocol callback은 인증된 bounded health payload의 의미만 소유한다. Protected state는 binding과
monotonic attempt sequence를 보존하고, update transaction은 exact pending identity의 active-slot
승격만 수행한다.

Protocol callback은 journal 또는 writer를 받지 않는다. Companion producer가 없는 OS protocol은
fail-closed한다. Exact duplicate만 idempotent success로 처리하며 새 attempt가 시작된 뒤의 과거
receipt는 거부한다.

## 검토한 대안

### Core에 OS별 success marker 추가

Generic ABI가 OS release와 서비스 구성에 종속되므로 기각했다.

### UEFI variable 하나로 nonce와 success만 저장

Product/protocol/image/manifest identity와 crash-consistent slot authority를 함께 증명하지 못하며
provider portability도 잃으므로 기각했다.

### Protocol callback이 slot을 직접 commit

Untrusted semantic parser가 update writer와 rollback authority를 갖게 되므로 기각했다.

## 결과

- 새 OS는 core 변경 없이 protocol health codec과 companion producer를 별도로 증명할 수 있다.
- 확인 key usage는 update-manifest signing과 분리된다.
- Protected/update journal 사이 reset은 동일 receipt 재전달로 수렴한다.
- Reference QEMU provider 결과는 hardware anti-replay 또는 실제 OS success 증거가 아니다.
