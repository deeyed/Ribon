---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-29
code_paths:
  - include/Ribon/firmware/environment.h
  - include/Ribon/protocol/entry_contract.h
  - include/Ribon/protocols/os/parus/rph1.h
  - src/environments/raw-fdt/
  - src/protocols/os/parus/
  - src/arch/riscv64/
  - targets/qemu-riscv64-virt-opensbi/
tests:
  - ribon-rph1-builder-unit
  - ribon-entry-bridge-unit
  - ribon-qemu-riscv64-rph1-fixture-smoke
hardware:
  - none
supersedes:
  - none
---

# ADR: RISC-V bootstrap hart 권한을 RPH1에 보존한다

## 맥락

OpenSBI가 S-mode next stage를 호출할 때 native input은 `a0`의 bootstrap hart ID와
`a1`의 FDT pointer다. Ribon이 Parus Boot Protocol을 선택한 뒤에는 같은 register가
`a0`의 RPH1 artifact pointer와 `a1`의 entry flags로 바뀐다.

두 ABI를 같은 의미로 취급하면 Ribon entry에서 받은 hart ID가 사라지거나 Parus가
RPH1 pointer를 hart ID로 해석할 수 있다. SBI HSM으로 시작하는 secondary hart의
`a0=hartid`, `a1=opaque` 규칙도 primary OS entry와 구분해야 한다.

## 결정

Ribon은 RISC-V firmware entry와 OS entry 사이에서 다음 세 경계를 분리한다.

1. OpenSBI native entry는 `(boot_cpu_id, machine_description_address)`로 정규화한다.
   QEMU virt/OpenSBI target entry는 `a0`와 `a1`을 BSS 초기화 전에 보존한다.
2. Raw-FDT environment는 bootstrap hart ID를
   `RibonBootEnvironment::boot_cpu_id`와 `RIBON_BOOT_ENV_HAS_BOOT_CPU_ID`로 seal한다.
3. Parus protocol은 RPH1 pointer와 flags만 primary OS entry register에 배치한다.
   Bootstrap hart ID는 RPH1 `BOOT_CPU` section으로 전달한다.

RPH1 `BOOT_CPU`는 type `0x000c`의 singleton, non-borrowed, 32-byte section이다.
RISC-V RPH1 product에서는 `REQUIRED_TO_UNDERSTAND`를 설정한다. AMD64와 AArch64
product는 architecture 계약이 요구하기 전 이 section을 만들지 않는다.

RISC-V normal Parus transfer는 S-mode, external interrupt masked, `satp=0`을 요구한다.
Ribon architecture bridge는 terminal jump 전에 instruction visibility와 address-space
전환 ordering을 확정하지만 permanent page table은 생성하지 않는다. Direct-high는
별도 capability가 열리기 전까지 unsupported다.

Ribon은 secondary hart를 시작하지 않는다. Parus가 runtime에서 SBI HSM을 호출할 때
secondary entry는 SBI가 정의한 `a0=hartid`, `a1=opaque`를 소비하며 RPH1 primary
entry ABI를 재사용하지 않는다.

## 결과

- Ribon Core와 generic Architecture ABI에는 OpenSBI나 Parus 의미가 들어가지 않는다.
- Raw-FDT environment는 native bootstrap CPU identity를 잃지 않는다.
- RPH1 producer와 consumer는 RISC-V boot CPU identity의 required section을 독립
  검증해야 한다.
- RISC-V UEFI application은 ExitBootServices 이전에 firmware boot-hart protocol로
  얻은 ID를 같은 environment fact에 투영해야 한다.
- Ribon QEMU fixture는 producer와 register bridge를 검증할 수 있지만 실제 Parus
  RISC-V consumer, SMP runtime 또는 물리 보드 지원을 증명하지 않는다.

## 기각한 대안

### `a2`에 hart ID를 추가 전달

RPH1 section과 register의 두 권위를 만들고 architecture마다 optional register
의미가 달라지므로 기각한다.

### RPH1 pointer 대신 OpenSBI native tuple을 유지

Parus protocol이 선택한 immutable handoff artifact를 우회하고 다른 ISA의 entry
contract와 달라지므로 기각한다.

### Ribon이 SBI HSM과 secondary hart를 소유

OS의 CPU topology, permanent translation state와 scheduler authority를 침범하므로
기각한다.
