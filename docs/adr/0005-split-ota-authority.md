---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-26
code_paths:
  - src/update/
  - src/recovery/
  - src/profiles/parus/
tests:
  - ribon-update-state-machine
  - ribon-update-power-loss-test
hardware:
  - none
supersedes:
  - none
---

# ADR: Runtime OTA와 boot commit 권한을 분리한다

## 맥락

Fleet rollout과 장기 network transaction은 OS runtime의 storage, time, policy가 필요하다.
반면 bootable slot 선택과 rollback은 OS가 실행되지 않는 상황에도 동작해야 한다.
한쪽에 모두 맡기면 정상 운영 정책이나 복구 신뢰 경계 중 하나가 약해진다.

## 결정

Parus updater는 fleet 정책, repository metadata, normal download와 inactive staging을
소유한다. Ribon은 manifest 재검증, anti-rollback, slot journal, pending/confirmed/failed
전이를 소유한다. Ribon recovery는 정상 slot 실패 때만 bounded fetch를 수행한다.

## 기각한 대안

### 모든 OTA를 Ribon에 구현

부트로더가 장기 network, fleet policy, certificate lifecycle을 소유해 normal image가
커지고 boot deadline이 불안정해지므로 선택하지 않는다.

### 모든 전환을 Parus에 맡김

손상된 OS가 active slot과 rollback metadata를 임의 변경할 수 있고 OS 불능 상태의 복구
경로가 사라지므로 선택하지 않는다.

### Active slot in-place update

Power loss와 partial write 뒤 known-good image를 보존할 수 없어 선택하지 않는다.

## 결과

- Signed bundle format은 Parus updater와 Ribon이 공유한다.
- Active confirmed slot은 overwrite하지 않는다.
- Confirmation이 없는 새 slot은 자동 승격되지 않는다.
- Power-loss fault injection이 release gate에 포함된다.
