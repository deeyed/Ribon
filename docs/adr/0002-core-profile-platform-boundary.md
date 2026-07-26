---
doc_type: adr
status: superseded
authority: historical
last_verified: 2026-07-26
code_paths:
  - include/Ribon/
  - src/core/
  - src/arch/
  - src/firmware/
  - src/profiles/
tests:
  - ribon-core-boundary-lint
hardware:
  - none
supersedes:
  - handoff-builder-only profile interface
superseded_by:
  - 0009-limine-library-plugin-hard-cut
---

# ADR: Generic Core와 OS·Platform module을 분리한다

## 맥락

OS handoff, firmware protocol, architecture entry, update service를 하나의 Core API에
넣으면 새 OS와 platform을 추가할 때 Core가 모든 조합의 조건문을 소유하게 된다.
반대로 profile이 block, network, watchdog을 직접 호출하면 OS 의미론이 platform 권한을
우회한다.

## 결정

Ribon은 Core, architecture, platform, service, profile을 독립 경계로 둔다. Core는 typed
descriptor와 operation table만 소비한다. Parus 의미론은 Parus profile만 소유한다.

Normal, recovery, provisioning, diagnostic mode는 link-time object graph와 runtime mode
selection을 모두 명시한다. Optional service는 capability descriptor 없이 호출할 수 없다.

## 기각한 대안

### 모든 기능을 profile callback으로 전달

Profile이 I/O와 security primitive를 소유하게 되어 Core가 검증 순서를 강제할 수 없으므로
선택하지 않는다.

### Platform별 Core fork

동일한 boot state machine이 UEFI, BIOS, board마다 갈라지고 failure semantics가 달라지므로
선택하지 않는다.

### 범용 service locator

Hidden dependency와 초기화 순서를 만들고 unsupported capability의 fail-closed 검증을
어렵게 하므로 선택하지 않는다.

## 결과

- Core public ABI의 operation과 descriptor가 먼저 동결되어야 한다.
- Platform native type은 adapter 경계를 넘지 않는다.
- Profile은 handoff와 OS component 의미론에 집중한다.
- Object-graph lint가 mode별 불필요한 service link를 거부한다.
