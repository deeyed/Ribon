---
doc_type: reference
status: accepted
authority: informative
last_verified: 2026-07-26
code_paths:
  - docs/contracts/update/
  - docs/contracts/network/
  - docs/contracts/platform/
  - docs/contracts/composition/
  - docs/contracts/firmware/
tests:
  - ribon-docs
hardware:
  - none
supersedes:
  - none
---

# 업데이트와 플랫폼 표준 참고 자료

이 문서는 Ribon 계약의 출처를 정리하며 자체적으로 Ribon 정책을 정의하지 않는다.

## Firmware update

- [IETF RFC 9019: A Firmware Update Architecture for Internet of Things](https://www.rfc-editor.org/rfc/rfc9019.html)
- [IETF RFC 9124: A Manifest Information Model for Firmware Updates](https://www.rfc-editor.org/rfc/rfc9124.html)
- [Uptane Standard](https://uptane.org/docs/latest/standard/uptane-standard)
- [MCUboot design](https://docs.mcuboot.com/design.html)

Ribon은 manifest authenticity, device compatibility, monotonic sequence, payload digest,
atomic state transition 원칙을 참고한다. Fleet repository policy 전체를 Ribon Core에
복제하지 않는다.

## Limine

- [Limine](https://github.com/limine-bootloader/limine)
- [Limine common runtime](https://github.com/limine-bootloader/limine/tree/v12.x/common)
- [Limine boot protocols](https://github.com/limine-bootloader/limine/tree/v12.x/common/protos)

Ribon은 common runtime과 Linux, Multiboot, chainload protocol 분리, BIOS와 architecture별
UEFI target 분리를 참고한다. Filename suffix와 source scan은 plugin SDK 정본으로
사용하지 않고 QStar manifest와 generated registry로 대체한다.

## EDK II

- [EDK II Module Write Guide](https://tianocore-docs.github.io/edk2-ModuleWriteGuide/draft/1_the_basics_of_edk_ii/11_overview.html)
- [EDK II Platforms](https://github.com/tianocore/edk2-platforms)

Ribon의 `plugin`, `package`, `product`, `image` metadata는 EDK II의 module, package,
platform, flash description 분리를 참고한다. EDK II 전체 phase와 protocol database를
Generic Core에 복제하지 않는다.

## UEFI와 network

- [UEFI Specification 2.11](https://uefi.org/specs/UEFI/2.11/)
- [UEFI network protocols](https://uefi.org/specs/UEFI/2.11/24_Network_Protocols_SNP_PXE_BIS.html)

UEFI environment consumer는 firmware protocol을 Ribon service로 정규화한다. UEFI
firmware personality는 반대 방향으로 service를 제공한다. Core는 EFI native type을
소비하지 않는다.

## RISC-V

- [RISC-V SBI specification](https://docs.riscv.org/reference/sbi/)
- [OpenSBI](https://github.com/riscv-software-src/opensbi)

OpenSBI는 M-mode와 SBI implementation을 소유하고 Ribon은 S-mode 또는 UEFI application
경계에 위치한다.

## Raspberry Pi

- [Raspberry Pi computer documentation](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html)

Secure boot, EEPROM boot flow, `tryboot` 같은 platform 기능은 Ribon bundle 검증과
결합할 수 있지만 Ribon Boot Protocol과 product contract를 대신하지 않는다.
