---
doc_type: contract
status: accepted
authority: normative
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
  - profile-preferred direct-high policy
---

# Higher-half 소유권 계약

Parus permanent higher-half address space는 Parus kernel이 소유한다. Ribon은 payload
layout을 검증하고 OS entry까지 실행을 유지하는 최소 entry bridge만 소유한다.

## Ribon 책임

Ribon architecture backend는 다음을 수행한다.

- ELF load address와 linked virtual address 관계 검증
- low physical entry stub의 executable range 검증
- handoff, stack, entry code 접근에 필요한 최소 mapping
- instruction/data cache synchronization
- interrupt mask와 privilege state 정규화
- entry register 설정

Entry bridge page table은 boot-transition lifetime을 갖는다. Ribon은 이를 Parus runtime
page table로 선언하거나 인수하도록 요구하지 않는다.

## Parus 책임

Parus는 다음을 소유한다.

- linker section별 RX, RO, RW와 W^X
- permanent higher-half page table
- high runtime stack과 guard
- high exception vector와 panic path
- identity map 축소와 TLB maintenance
- runtime MMIO window
- secondary CPU의 permanent translation state

RPH1 `KERNEL_IMAGE_LAYOUT`은 이 정책의 입력이지 page-table authority가 아니다.

## Architecture별 bridge

| Architecture | Ribon entry bridge |
| --- | --- |
| AMD64 UEFI | long mode 유지에 필요한 최소 identity mapping |
| AMD64 BIOS | protected/long mode 전환과 최소 identity mapping |
| AArch64 | firmware MMU/cache/EL 상태를 정규화한 low entry |
| RISC-V 64 | OpenSBI 뒤 S-mode, `satp=BARE`를 우선하는 low entry |

## Direct-high capability

Direct-high는 optional architecture capability다. Parus normal release profile의 필수
경로가 아니다. Diagnostic 또는 명시적으로 선택된 target에서만 다음 조건으로 허용한다.

- profile이 virtual entry와 mapping recipe를 명시한다.
- architecture backend가 entry state를 검증한다.
- `ENTERED_HIGH`와 `DIRECT_HIGH` flag를 함께 전달한다.
- Parus가 entry state와 mapping을 다시 검증한다.
- Ribon page table의 runtime ownership을 주장하지 않는다.

Direct-high 실패는 low entry로 암묵 fallback하지 않는다. Fallback은 별도 target policy와
독립적으로 검증된 low entry를 요구한다.
