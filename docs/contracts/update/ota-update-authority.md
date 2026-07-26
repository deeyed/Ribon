---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-26
code_paths:
  - src/core/
  - src/update/
  - src/trust/
  - src/profiles/parus/
tests:
  - ribon-update-state-machine
  - ribon-update-power-loss-test
  - ribon-update-rollback-test
hardware:
  - none
supersedes:
  - none
---

# OTA 업데이트 권한 계약

OTA의 fleet 정책과 runtime 다운로드는 Parus updater가 소유하고, bootable slot의 검증과
전환은 Ribon update service가 소유한다. Ribon recovery는 Parus가 실행되지 못하는
상황에서만 제한된 다운로드 권한을 갖는다.

## 권한 분리

| 책임 | 소유자 |
| --- | --- |
| fleet rollout, scheduling, operator policy | Parus updater |
| repository metadata와 endpoint 선택 | Parus updater |
| inactive slot staging 요청 | Parus updater |
| manifest signature와 component digest 재검증 | Ribon |
| anti-rollback sequence | Ribon + platform protected state |
| pending/confirmed/failed 전이 | Ribon |
| health confirmation | Parus health service |
| 정상 slot 실패 후 recovery fetch | Ribon recovery |

Parus가 이미 검증했다는 표시는 Ribon 검증을 생략하게 하지 않는다.

## Bundle manifest

Boot bundle은 다음 signed 정보를 포함한다.

- manifest format version과 monotonic sequence
- vendor, product, board class와 hardware revision
- architecture와 OS profile
- Ribon 및 Parus ABI 범위
- component type, size, digest, destination
- kernel, device tree, module, command-line policy의 결합
- predecessor 조건
- signing key ID와 delegation
- recovery 및 rollback policy

Transport URI와 mirror는 서명된 정책 또는 provisioned endpoint에서만 얻는다.

## Slot 상태

```text
EMPTY -> STAGING -> VERIFIED -> PENDING -> CONFIRMED
                    |           |
                    v           v
                  FAILED <---- FAILED
```

`STAGING`과 `FAILED` slot은 실행하지 않는다. `VERIFIED`는 전체 component와 manifest
검증이 성공했음을 뜻한다. `PENDING`은 제한 횟수의 시험 부팅만 허용한다.

## Write 순서

1. active confirmed slot과 다른 destination을 선택한다.
2. `STAGING` generation을 redundant metadata에 기록한다.
3. Payload를 bounded chunk로 기록하며 digest를 누적한다.
4. Storage flush 뒤 전체 component를 다시 읽어 검증한다.
5. Manifest와 compatibility, sequence를 검증한다.
6. `VERIFIED`를 commit한다.
7. 별도 commit으로 `PENDING`을 연다.

모든 단계에서 power loss 뒤 active confirmed slot과 이전 committed metadata generation을
복원할 수 있어야 한다.

## Anti-rollback

Manifest sequence는 wrap하지 않는 unsigned monotonic 값이다. Protected sequence보다 작은
manifest는 서명이 유효해도 거부한다. 승인된 기능 rollback은 이전 payload에 더 큰 sequence를
부여한 새 manifest로만 수행한다.

## 비목표

Ribon은 fleet rollout graph, 사용자 consent, 대규모 repository delegation, 장기 download
retry를 소유하지 않는다. Uptane/TUF 전체 client는 Parus runtime에 둔다.
