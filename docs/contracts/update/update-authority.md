---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-08-01
code_paths:
  - src/plugins/update/
  - src/plugins/policy/
  - src/plugins/storage/
  - src/protocols/
  - products/
  - include/Ribon/security/protected_state.h
  - include/Ribon/update/manifest.h
  - src/security/protected_state.c
  - src/update/manifest.c
tests:
  - ribon-update-state-machine
  - ribon-update-power-loss-test
  - ribon-update-rollback-test
  - make check-security-protected-state
  - make check-update-manifest
hardware:
  - none
supersedes:
  - ota-update-authority
  - OS-fixed update authority
---

# 업데이트 권한 계약

Ribon Core는 fleet updater가 아니다. Boot-critical update authority는 selected update,
storage, security, boot-policy plugin이 함께 제공하며 OS runtime 정책은 OS-specific
companion component가 소유한다.

## 권한 분리

| 책임 | 소유자 |
| --- | --- |
| fleet rollout, schedule, operator policy | OS 또는 management component |
| repository와 endpoint 선택 | OS 또는 provisioned recovery policy |
| normal runtime download | OS updater |
| inactive destination write | selected storage/update plugin |
| manifest signature와 digest 검증 | selected security plugin과 Boot Library |
| anti-rollback sequence | update policy와 protected state provider |
| pending/confirmed/failed 전이 | update policy |
| health payload 의미론 | selected Boot Protocol |
| recovery fetch | recovery product의 transport policy |

OS가 검증했다는 표시는 Boot Library의 독립 검증을 생략하게 하지 않는다.

## Bundle manifest

Signed manifest는 다음 정보를 포함한다.

- format version과 monotonic sequence
- vendor, product, platform class, hardware revision
- architecture, environment, Boot Protocol ABI 범위
- component role, size, digest, destination policy
- image format과 entry compatibility
- predecessor와 rollback 조건
- signing key ID와 delegation
- single key usage와 rollback-domain ID
- recovery와 confirmation policy

Transport URI와 mirror는 서명된 정책 또는 provisioned endpoint에서 얻는다.

Canonical wire, update-only signed message, detached signature envelope와 independent reader의
세부 계약은 {doc}`signed-update-manifest-v1`이 소유한다. Source JSON이나 native C struct를 target
manifest wire로 사용하지 않는다.

Inactive media의 deterministic A/B range, exact LE slot metadata, active-slot protection과 semantic
writer handle은 {doc}`../storage/bounded-update-slot-provider-v1`이 소유한다. Manifest authorization은
storage offset을 만들지 않으며 storage provider는 manifest signature나 rollback authority를
판정하지 않는다.

Product-bound manifest 승인부터 component full readback과 `VERIFIED` successor까지의 exact
transaction은 {doc}`signed-bundle-install-v1`이 소유한다. Firmware adapter는 storage mechanism만
제공하고 installer의 update 의미를 복제하지 않는다.

`STAGING` durable commit부터 payload flush, `VERIFIED`, `PENDING`과 crash resume까지의 ordering은
{doc}`transaction-journal-v1`이 소유한다. Update journal의 `PENDING`은 protected-state trial과
attempt 소비가 commit되기 전에는 boot authority가 아니다.

## Write 순서

1. Active confirmed destination과 다른 inactive destination을 선택한다.
2. Manifest signature, product compatibility와 rollback sequence를 write 전에 검증한다.
3. `STAGING` successor를 transaction journal에 commit하고 reopen한다.
4. Payload를 bounded chunk로 기록하며 source digest를 검증한다.
5. 모든 component write를 flush한 뒤 전체 component를 다시 읽어 검증한다.
6. `VERIFIED`를 journal에 commit하고 재개방한다.
7. 별도 journal commit으로 `PENDING`을 연다.
8. Protected state의 exact successor trial과 attempt를 commit한 뒤에만 transfer authority를 연다.

Partial payload와 `STAGING` destination을 실행하지 않는다.

## Anti-rollback

Sequence는 domain별 wrap하지 않는 unsigned monotonic 값이다. Confirmed floor가 `N`이면 새
pending candidate는 정확히 `N+1`이어야 한다. Trial 동안 confirmed `N`과 pending `N+1`만
실행 authority를 가지며 confirmation commit 뒤에는 `N`을 normal authority에서 거부한다.
승인된 기능 rollback도 더 큰 sequence의 새 manifest를 요구한다.

Update manifest signature는 `UPDATE_MANIFEST` single key usage와 exact product, mode,
rollback-domain 및 sequence에 결속된다. Policy, image와 recovery key usage를 update manifest
authority로 재사용하지 않는다.

## Protocol confirmation

OS companion은 선택된 Boot Protocol이 소유하는 health payload를 생성한다. Protocol callback은
인증된 bounded payload의 의미만 검증하며 slot, key, journal을 변경하지 않는다. Generic
confirmation engine은 product/protocol/version, slot, image generation, policy version, manifest
digest/sequence, nonce와 attempt sequence를 독립 검증하고 `BOOT_CONFIRMATION` 전용 key usage를
요구한다.

승격 authority는 {doc}`boot-confirmation-v1`의 순서대로 protected-state binding과 update
transaction을 모두 닫는다. Exact duplicate는 idempotent success지만 stale attempt와 다른 identity는
실패한다. OS 이름, 서비스 이름과 OS 내부 success marker는 generic update ABI에 들어가지 않는다.

## 비목표

Generic Ribon product는 fleet rollout graph, 사용자 consent, 장기 repository delegation,
무제한 download retry를 소유하지 않는다. OS-specific overseer와 updater는 plugin 또는
companion package이며 Core ABI를 확장하지 않는다.
