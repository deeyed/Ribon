---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-26
code_paths:
  - src/core/
  - src/recovery/
  - src/platform/
  - src/profiles/parus/
tests:
  - ribon-watchdog-reset-policy
  - ribon-boot-confirmation-test
hardware:
  - none
supersedes:
  - none
---

# Overseer와 watchdog 모델

Ribon overseer는 reset 경계에서 동작하는 복구 권한자다. Ribon Core는 OS 실행 중에
상주하는 hypervisor가 아니다.

## 감독 cycle

1. Ribon이 boot generation과 nonce를 생성한다.
2. Attempt를 journal에 commit한다.
3. Platform watchdog을 bounded deadline으로 arm한다.
4. Parus가 실행되고 health gate를 수행한다.
5. Health service가 generation과 nonce를 포함한 confirmation을 기록한다.
6. 다음 reset에서 Ribon이 confirmation과 reset reason을 검증한다.

Watchdog reset과 미확인 pending slot은 실패 attempt다. 확인된 slot의 watchdog reset은
crash policy에 따라 failure budget을 소비한다.

## Confirmation

Confirmation은 다음 값을 포함한다.

- slot ID
- metadata generation
- boot nonce
- manifest sequence
- Parus health-policy version
- integrity code 또는 platform-protected commit

이전 boot의 confirmation을 replay할 수 없어야 한다. Confirmation write가 torn되면
미확인으로 취급한다.

## 외부 safety controller

Autonomous-machine platform은 SoC와 독립된 safety controller를 둘 수 있다. Safety
controller는 actuator inhibit, heartbeat deadline, power cycle, emergency stop을
소유한다. Ribon은 boot generation, expected deadline, update-in-progress, safe-to-transfer
상태만 전달한다.

Ribon은 motor, ESC, PWM을 직접 구동하지 않는다. Parus health 확인 전 actuator enable을
요청하지 않는다.

## Resident monitor 경계

EL2, M-mode, SMM, VMX root에 상주하는 monitor는 별도 제품과 ABI다. 해당 monitor는
memory isolation, interrupt routing, timer virtualization, DMA, hypercall을 독립 계약으로
소유해야 한다. Ribon Core에 resident monitor 권한을 추가하지 않는다.

RISC-V M-mode는 SBI firmware 권한과 충돌하지 않으며, ARM EL3와 x86 SMM도 platform
firmware ownership을 우회하지 않는다.
