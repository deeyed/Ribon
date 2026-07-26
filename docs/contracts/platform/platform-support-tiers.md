---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-26
code_paths:
  - src/arch/
  - src/firmware/
  - src/boot/
  - targets/
tests:
  - ribon-platform-tier-lint
hardware:
  - none
supersedes:
  - primary-future architecture labels
---

# 플랫폼 지원 등급 계약

지원 등급은 architecture와 firmware 조합의 완료 의무를 정의한다. 등급은 실행 성공
주장이 아니며 각 조합은 독립된 evidence를 요구한다.

## 등급

| 등급 | 의미 |
| --- | --- |
| `PRIMARY` | release와 회귀 gate가 요구하는 주력 경로 |
| `STRATEGIC` | 1급 architecture 경계를 유지하고 독립 acceptance gate를 닫는 경로 |
| `COMPATIBILITY` | 제한된 호환 목적이며 platform security 제약을 명시하는 경로 |
| `EXPERIMENTAL` | build 또는 research 전용, production claim 금지 |

## 조합

| Architecture | Firmware/platform | 등급 | 소유 경계 |
| --- | --- | --- | --- |
| x86_64 | UEFI | `PRIMARY` | UEFI Boot Services와 minimal entry bridge |
| AArch64 | UEFI | `PRIMARY` | UEFI와 AArch64 entry normalization |
| AArch64 | Raspberry Pi 5 native | `PRIMARY` | firmware handoff, DTB, package, live UART |
| RISC-V 64 | OpenSBI | `STRATEGIC` | S-mode entry, FDT, SBI extension |
| RISC-V 64 | UEFI | `STRATEGIC` | UEFI application과 Parus profile |
| x86_64 | legacy BIOS | `COMPATIBILITY` | EDD, E820, long-mode bridge, optional PXE |

## 공통 acceptance

각 조합은 다음 증거를 독립적으로 닫는다.

1. architecture 및 platform unit
2. artifact build와 graph 검증
3. 실제 Parus payload load
4. Parus Handoff v1 consumer acceptance
5. bounded boot marker와 raw evidence
6. 대상이 물리 board면 fresh hardware capture

UEFI QEMU 결과는 BIOS, RPi5, RISC-V board 결과를 대신하지 않는다.

## BIOS 보안 경계

BIOS Ribon이 boot bundle을 검증해도 Ribon 자신에 대한 hardware root of trust는 자동으로
생기지 않는다. TPM measurement, verified firmware, write protection 같은 platform
보호가 없으면 secure-boot production claim을 열지 않는다.

## RISC-V 권한

OpenSBI가 M-mode와 SBI를 소유한다. Ribon은 S-mode 또는 UEFI application으로 동작하고
Parus secondary hart 정책을 대신하지 않는다. HSM을 통한 secondary 시작은 Parus runtime
책임이다.
