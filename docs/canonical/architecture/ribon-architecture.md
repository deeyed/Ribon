---
doc_type: canonical
status: accepted
authority: normative
last_verified: 2026-07-26
code_paths:
  - include/Ribon/
  - src/core/
  - src/loader/
  - src/arch/
  - src/firmware/
  - src/profiles/
tests:
  - core_service_boundary_tests
  - object_graph_lint
  - ribon-documentation-quality-lint
hardware:
  - none
supersedes:
  - docs/ARCHITECTURE.md
---

# Ribon 구조

Ribon은 OS에 중립적인 결정론적 Core, architecture backend, platform adapter,
공통 service, OS profile, 실행 mode를 분리한다.

## 책임 계층

### Core

Core는 고정 용량 boot state machine, normalized memory map, component plan, slot 선택,
검증 순서, 실패 전이를 소유한다. Core는 OS handoff의 wire field와 platform protocol을
직접 해석하지 않는다.

### Architecture backend

Architecture backend는 CPU entry state, canonical address, cache maintenance, privilege
level, 최소 entry bridge를 소유한다. OS의 permanent virtual-memory 정책은 소유하지
않는다.

### Platform adapter

Platform adapter는 UEFI, BIOS, board firmware, SBI 같은 실행 환경을 typed service로
번역한다. Core는 firmware header와 MMIO address를 직접 include하지 않는다.

### 공통 service

공통 service는 image format, cryptographic verification, block I/O, slot journal,
network transport, watchdog, reset reason을 제공한다. 각 service는 명시적으로 선택되어
정상 boot 또는 recovery object graph에 포함된다.

### OS profile

OS profile은 kernel component 조합, entry contract, handoff format, boot confirmation
의미론을 소유한다. Parus 규칙은 Parus profile에만 존재한다.

## 의존성 방향

```text
frontend -> platform/arch services -> Core -> selected OS profile
                                  \-> image/trust/update services
```

Core는 Parus header, UEFI header, BIOS declaration, SBI declaration을 include하지 않는다.
Profile은 Core public contract에 의존할 수 있지만 Core가 profile 구현에 역으로
의존하지 않는다.

## 실행 mode

Ribon은 다음 mode를 구분한다.

| Mode | 네트워크 | mutable storage | 목적 |
| --- | --- | --- | --- |
| normal boot | disabled | boot-attempt journal만 허용 | 검증된 slot을 bounded time 안에 실행 |
| recovery | 명시적으로 허용 | inactive/recovery slot만 허용 | 정상 slot 실패 복구 |
| provisioning | build 및 physical-presence 정책으로 제한 | trust anchor와 초기 metadata | 제조·등록 |
| diagnostic | 별도 object graph | evidence 전용 | 개발 및 검증 |

Mode 선택은 Core boot state machine의 입력인 동시에 link object graph의 입력이다.
Binary는 정확히 한 mode descriptor를 제공한다. Diagnostic 코드가 normal boot의
정책이나 storage authority를 대신하지 않는다.

## 결정성

모든 parser, loader, network transaction, journal operation은 다음 상한을 갖는다.

- 입력 byte 수
- table 및 component 수
- allocation 또는 arena 사용량
- retry 수
- deadline
- output 크기

정상 boot는 네트워크 가용성에 의존하지 않는다. 손상된 입력과 지원되지 않는
capability는 명시적 오류를 내고 fail-closed 전이한다.

Core의 동적 storage는 caller-owned fixed arena 하나로 제한한다. Mode descriptor는
arena, input, handoff, table, component, retry, deadline 상한을 함께 제공한다.
Platform service는 supported와 unsupported capability를 전부 분류하며 callback만
보고 지원 여부를 추론하지 않는다.

## 비목표

Ribon Core는 다음 책임을 갖지 않는다.

- flight control과 actuator 제어
- Parus scheduler, driver, executor 정책
- 범용 shell과 server
- permanent kernel page table
- fleet rollout 정책
- 장기 resident hypervisor
