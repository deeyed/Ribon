---
doc_type: canonical
status: accepted
authority: normative
last_verified: 2026-07-26
code_paths:
  - src/core/
  - src/boot/
  - src/firmware/
  - src/profiles/
tests:
  - ribon-boot-lifecycle-state-test
  - ribon-update-power-loss-test
hardware:
  - none
supersedes:
  - none
---

# 부트·업데이트·복구 생명주기

Ribon은 platform trust root와 OS runtime 사이에서 image 선택, 검증, 원자적 전환,
실패 복구를 수행한다.

## 정상 부트

정상 부트는 다음 순서를 따른다.

1. platform과 architecture state를 수집한다.
2. redundant metadata에서 유효 generation을 선택한다.
3. bootable slot과 attempt policy를 결정한다.
4. manifest와 component를 검증한다.
5. payload를 겹치지 않는 물리 range에 배치한다.
6. OS profile이 handoff를 생성한다.
7. attempt를 journal에 commit한다.
8. watchdog 계약을 설정하고 OS entry로 이전한다.

검증되지 않은 component는 load 또는 execute하지 않는다.

## 업데이트

업데이트는 active slot을 덮어쓰지 않는다. Payload는 inactive slot에 streaming 방식으로
기록하고 component digest와 전체 manifest를 검증한 뒤 `verified` 상태로 승격한다.
Power loss가 어느 write 경계에서 발생하더라도 active confirmed slot은 bootable해야 한다.

## 시험 부팅과 확인

새 slot은 `pending` 상태로 시험 부팅한다. Ribon은 jump 전에 attempt를 차감한다.
Parus는 필수 health gate를 통과한 뒤에만 boot generation과 nonce를 포함한 confirmation을
기록한다. Confirmation이 없거나 watchdog reset이 발생하면 Ribon은 attempt policy에 따라
rollback한다.

## 복구

복구 mode는 정상 slot이 유효하지 않거나 명시적인 복구 trigger가 있을 때만 열린다.
복구 mode도 같은 trust root, manifest, compatibility, anti-rollback 규칙을 적용한다.
전송 성공만으로 slot을 bootable로 만들지 않는다.

## 감독

Ribon의 감독 권한은 reset 경계를 중심으로 동작한다. Parus liveness는 hardware watchdog이
감시하고 reset 뒤의 Ribon이 reset reason, attempt, confirmation을 평가한다. SoC 밖의
actuator 안전은 독립된 safety controller가 소유한다.
