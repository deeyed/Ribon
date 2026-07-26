---
doc_type: reference
status: accepted
authority: informative
last_verified: 2026-07-26
code_paths:
  - docs/contracts/update/
  - docs/contracts/network/
  - docs/contracts/platform/
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

## UEFI와 network

- [UEFI Specification 2.11](https://uefi.org/specs/UEFI/2.11/)
- [UEFI network protocols](https://uefi.org/specs/UEFI/2.11/24_Network_Protocols_SNP_PXE_BIS.html)

UEFI adapter는 firmware protocol을 platform service로 정규화한다. Core는 EFI native
type을 소비하지 않는다.

## RISC-V

- [RISC-V SBI specification](https://docs.riscv.org/reference/sbi/)
- [OpenSBI](https://github.com/riscv-software-src/opensbi)

OpenSBI는 M-mode와 SBI implementation을 소유하고 Ribon은 S-mode 또는 UEFI application
경계에 위치한다.

## Raspberry Pi

- [Raspberry Pi computer documentation](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html)

Secure boot, EEPROM boot flow, `tryboot` 같은 platform 기능은 Ribon bundle 검증과
결합할 수 있지만 Ribon profile contract를 대신하지 않는다.
