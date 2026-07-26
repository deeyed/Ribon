---
doc_type: adr
status: superseded
authority: historical
last_verified: 2026-07-26
code_paths:
  - src/arch/
  - src/profiles/parus/
  - ../../../../sys/arch/
  - ../../../../sys/kern/vm/
tests:
  - ribon-entry-bridge-unit
  - parus-kernel-owned-higher-half
hardware:
  - none
supersedes:
  - direct-high-preferred Parus profile
superseded_by:
  - 0009-limine-library-plugin-hard-cut
---

# ADR: Parus permanent higher-half는 kernel이 소유한다

## 맥락

Ribon이 Parus permanent page table까지 구성하면 linker section permission, high stack,
exception vector, identity lifetime을 bootloader와 kernel이 함께 소유한다. Architecture가
늘어날수록 Core와 OS profile에 VM 정책이 중복된다.

## 결정

Ribon은 OS entry를 유지하는 최소 architecture bridge만 만든다. Parus permanent
higher-half, W^X, stack, vector, identity shrink는 Parus EB3와 architecture backend가
소유한다.

Ribon은 virtual layout을 검증해 RPH1로 전달한다. Direct-high는 optional diagnostic
capability이며 normal Parus release의 요구사항이 아니다.

## 기각한 대안

### Ribon page table을 Parus가 인수

Page-table allocator, permission, reclaim authority가 handoff를 가로질러 공유되므로
선택하지 않는다.

### 모든 architecture에서 direct-high 강제

BIOS, UEFI, board firmware, OpenSBI의 entry state 차이를 Parus profile이 떠안게 되므로
선택하지 않는다.

### Ribon이 paging을 전혀 다루지 않음

AMD64 long mode와 firmware-independent entry continuity를 보장할 수 없어 선택하지 않는다.

## 결과

- AMD64와 BIOS는 최소 identity bridge를 유지한다.
- Parus low entry stub가 permanent transition의 시작점이 된다.
- Direct-high gate는 별도 target과 evidence를 요구한다.
- Bootloader page table은 runtime VM claim을 열지 않는다.
