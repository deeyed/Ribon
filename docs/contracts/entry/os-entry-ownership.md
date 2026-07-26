---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-26
code_paths:
  - src/arch/
  - src/protocols/
  - src/common/boot/
tests:
  - ribon-entry-bridge-unit
  - ribon-protocol-entry-contract-test
  - ribon-os-owned-address-space-test
hardware:
  - none
supersedes:
  - higher-half-ownership
  - profile-preferred direct-high policy
---

# OS entry와 address-space 소유권 계약

Permanent OS address space는 booted OS가 소유한다. Ribon은 payload layout을 검증하고
entry까지 실행 연속성을 유지하는 최소 architecture bridge만 소유한다.

## Ribon 책임

Architecture Backend는 다음을 수행한다.

- image-format plugin이 반환한 load와 virtual address 관계 검증
- executable entry range 검증
- handoff, stack, entry code에 필요한 최소 mapping
- instruction/data cache synchronization
- interrupt mask와 privilege state 정규화
- Boot Protocol이 선택한 register ABI 적용

Entry bridge page table은 boot-transition lifetime을 갖는다. Ribon은 이를 OS runtime
page table로 선언하거나 OS가 인수하도록 요구하지 않는다.

## Boot Protocol 책임

Protocol은 다음을 선언한다.

- physical, virtual, runtime entry candidate
- required privilege state
- required register ABI
- handoff pointer와 flag
- direct-high 허용 여부
- architecture-specific validation hook

Protocol은 permanent page table을 생성하지 않는다.

## OS 책임

OS는 다음을 소유한다.

- linker section별 RX, RO, RW와 W^X
- permanent page table
- runtime stack과 guard
- exception vector와 panic path
- identity map 축소와 TLB maintenance
- runtime MMIO
- secondary CPU의 permanent translation state

Handoff image-layout descriptor는 이 정책의 입력이지 page-table authority가 아니다.

## Architecture bridge

| Architecture/environment | 최소 bridge |
| --- | --- |
| x86_64 UEFI application | long mode 연속성과 최소 identity mapping |
| x86 BIOS client | protected/long-mode transition과 최소 identity mapping |
| AArch64 UEFI/raw-FDT | firmware MMU, cache, EL state 정규화 |
| RISC-V SBI | S-mode와 `satp` entry contract 정규화 |

## Direct-high

Direct-high는 optional protocol과 architecture capability다. Normal product의 공통
필수 경로가 아니다.

- Protocol이 virtual entry와 mapping recipe를 명시한다.
- Architecture Backend가 entry state를 검증한다.
- Handoff flag가 실제 CPU state와 일치한다.
- OS가 entry state와 mapping을 다시 검증한다.
- Ribon mapping의 runtime ownership을 주장하지 않는다.

Direct-high 실패는 low entry로 암묵 fallback하지 않는다. Fallback은 product manifest가
선택한 별도 entry contract와 독립 검증을 요구한다.
