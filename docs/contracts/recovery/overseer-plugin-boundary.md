---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-26
code_paths:
  - src/plugins/policy/
  - src/plugins/watchdog/
  - src/plugins/update/
  - products/
tests:
  - ribon-watchdog-reset-policy
  - ribon-boot-confirmation-test
  - ribon-overseer-object-graph-lint
hardware:
  - none
supersedes:
  - overseer-watchdog-model
  - Ribon-core-resident overseer
---

# Overseer plugin과 watchdog 경계

Generic Ribon Core는 OS liveness policy를 소유하지 않는다. Overseer는 Ribon library,
watchdog, update journal, OS Boot Protocol confirmation을 조합하는 OS-specific policy
plugin 또는 companion product다.

## Generic supervision primitive

Ribon SDK는 다음 OS-neutral primitive만 제공한다.

- boot generation과 nonce
- attempt journal
- watchdog arm과 reset reason
- confirmation envelope storage
- candidate failure budget
- rollback과 recovery transition hook

Primitive는 healthy의 OS별 의미를 정의하지 않는다.

## OS-specific overseer

Overseer plugin은 다음을 정의할 수 있다.

- health-policy version
- OS service와 subsystem readiness 조건
- confirmation payload
- boot deadline과 retry policy
- recovery 또는 diagnostic product 선택
- operator와 fleet 상태 변환

Parus overseer는 LUCA protocol 또는 companion package에 위치한다. Linux와 FreeBSD
product에는 Parus overseer object를 링크하지 않는다.

## Reboot-time cycle

1. Policy가 generation과 nonce를 생성한다.
2. Attempt journal을 commit한다.
3. Watchdog provider를 bounded deadline으로 arm한다.
4. OS가 protocol-specific health gate를 수행한다.
5. OS component가 confirmation을 기록한다.
6. Reset 뒤 policy가 confirmation, reset reason, attempt를 검증한다.

Watchdog reset과 미확인 pending candidate는 실패 attempt다. 이전 generation과 nonce의
confirmation을 replay할 수 없어야 한다.

## 외부 safety controller

Autonomous-machine product는 SoC와 독립된 safety controller를 둘 수 있다. Safety
controller는 actuator inhibit, heartbeat deadline, power cycle, emergency stop을
소유한다.

Ribon은 boot generation, expected deadline, update-in-progress, safe-to-transfer 상태만
전달한다. Motor, ESC, PWM을 직접 구동하지 않는다.

## Resident monitor

EL2, M-mode, SMM, VMX root에 상주하는 monitor는 별도 product와 ABI다. Memory isolation,
interrupt routing, timer virtualization, DMA, hypercall을 독립 계약으로 소유한다.
Bootloader product나 `libribon-core`에 resident monitor 권한을 추가하지 않는다.
