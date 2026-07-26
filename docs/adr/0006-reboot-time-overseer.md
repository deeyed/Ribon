---
doc_type: adr
status: superseded
authority: historical
last_verified: 2026-07-26
code_paths:
  - src/core/
  - src/recovery/
  - src/platform/
tests:
  - ribon-watchdog-reset-policy
  - ribon-boot-confirmation-test
hardware:
  - none
supersedes:
  - none
superseded_by:
  - 0009-limine-library-plugin-hard-cut
---

# ADR: Ribon overseer는 reboot-time 권한으로 둔다

## 맥락

Kernel로 jump한 부트로더는 실행되지 않으므로 별도 privilege 설계 없이 OS liveness를
계속 감시할 수 없다. Ribon을 EL2, M-mode, SMM에 상주시키면 hypervisor와 firmware
책임까지 포함하게 된다.

## 결정

Hardware watchdog이 Parus liveness를 감시하고 reset 뒤 Ribon이 reset reason, attempt,
confirmation, slot generation을 평가한다. Ribon은 rollback, recovery, signed diagnostic
payload 선택을 수행한다.

Actuator 안전과 SoC power cycle은 독립 safety controller가 소유할 수 있다. Resident
monitor가 필요하면 Ribon Core와 다른 artifact, ABI, threat model로 개발한다.

## 기각한 대안

### Ribon Core의 EL2/M-mode 상주

Interrupt, timer, DMA, memory isolation, hypercall이 Core에 들어와 generic bootloader
경계를 무너뜨리므로 선택하지 않는다.

### Parus heartbeat file만 확인

Replay와 torn write를 구분할 boot nonce와 generation이 없어 선택하지 않는다.

### Software timer만 사용

SoC hang에서 실행되지 않으므로 hardware watchdog을 대신할 수 없어 선택하지 않는다.

## 결과

- Boot confirmation은 nonce와 generation을 포함한다.
- Watchdog reset은 slot failure budget에 반영된다.
- Safety controller protocol은 Ribon·Parus와 독립된 안전 계약을 갖는다.
- Ribon normal image는 hypervisor 기능을 포함하지 않는다.
