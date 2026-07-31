---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-26
code_paths:
  - src/plugins/update/
  - src/plugins/policy/
  - src/plugins/storage/
  - src/protocols/
  - products/
tests:
  - ribon-update-state-machine
  - ribon-update-power-loss-test
  - ribon-update-rollback-test
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

## Write 순서

1. Active confirmed destination과 다른 inactive destination을 선택한다.
2. `STAGING` generation을 redundant metadata에 기록한다.
3. Payload를 bounded chunk로 기록하며 digest를 누적한다.
4. Flush 뒤 전체 component를 다시 읽어 검증한다.
5. Manifest, product compatibility, sequence를 검증한다.
6. `VERIFIED`를 commit한다.
7. 별도 commit으로 `PENDING`을 연다.

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

Boot Protocol은 OS-specific health payload를 생성하고 검증할 수 있다. Update policy는
protocol ID, slot, generation, nonce, manifest sequence가 candidate와 일치할 때만
confirmation을 승인한다.

## 비목표

Generic Ribon product는 fleet rollout graph, 사용자 consent, 장기 repository delegation,
무제한 download retry를 소유하지 않는다. OS-specific overseer와 updater는 plugin 또는
companion package이며 Core ABI를 확장하지 않는다.
