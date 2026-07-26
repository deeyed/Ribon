---
doc_type: adr
status: superseded
authority: historical
last_verified: 2026-07-26
code_paths:
  - src/arch/x86_64/
  - src/arch/riscv64/
  - src/firmware/bios/
  - src/firmware/uefi/
  - src/boot/
tests:
  - ribon-platform-tier-lint
hardware:
  - none
supersedes:
  - primary-future architecture classification
superseded_by:
  - 0009-limine-library-plugin-hard-cut
---

# ADR: BIOS와 RISC-V 지원 등급을 분리한다

## 맥락

BIOS는 넓은 x86 호환성을 제공하지만 hardware root of trust가 일관되지 않다. RISC-V는
portable architecture 목표에 중요하지만 M-mode firmware와 S-mode bootloader 책임을
구분해야 한다. 두 경로를 같은 future stub 등급으로 두면 구현 의무가 불분명하다.

## 결정

RISC-V OpenSBI와 UEFI 경로는 `STRATEGIC` 1급 architecture boundary로 개발한다. OpenSBI가
M-mode와 SBI를 소유하고 Ribon은 S-mode 또는 UEFI application으로 동작한다.

Legacy BIOS는 `COMPATIBILITY` 경로로 구현한다. EDD, E820, long-mode bridge, Parus Handoff
v1, optional PXE를 제공하되 platform root of trust가 없으면 secure production claim을
열지 않는다.

## 기각한 대안

### RISC-V M-mode를 Ribon이 직접 소유

OpenSBI와 SBI ecosystem을 중복하고 overseer 권한이 Core로 유입되므로 선택하지 않는다.

### BIOS 지원 제거

Recovery, emulator, legacy x86 validation surface를 잃으므로 선택하지 않는다.

### BIOS를 UEFI와 같은 보안 등급으로 취급

Ribon 자체의 authenticity를 보장할 platform root가 다르므로 선택하지 않는다.

## 결과

- RISC-V는 QEMU virt + OpenSBI를 첫 acceptance 환경으로 사용한다.
- Secondary hart 시작은 Parus가 SBI HSM을 통해 소유한다.
- BIOS는 SeaBIOS/QEMU와 별도 hardware evidence를 구분한다.
- Support tier는 implementation status나 hardware claim이 아니다.
