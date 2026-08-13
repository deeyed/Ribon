---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-08-02
code_paths:
  - src/arch/
  - src/environments/
  - src/image-formats/
  - ports/
  - products/
  - targets/
tests:
  - ribon-target-tier-lint
  - ribon-product-composition-test
hardware:
  - none
supersedes:
  - architecture-firmware support tier
---

# Product Target 지원 등급 계약

지원 등급은 architecture 하나나 firmware 이름이 아니라 완전한 product target의
유지 의무를 정의한다.

```text
product + architecture + environment 또는 personality
        + typed port services + protocol set + image recipe
```

등급은 실행 성공 주장이 아니며 target마다 독립 evidence를 요구한다.

## 등급

| 등급 | 유지 의무 |
| --- | --- |
| `PRIMARY` | release build와 상시 회귀 gate |
| `STRATEGIC` | 1급 ABI 경계와 독립 acceptance |
| `COMPATIBILITY` | 제한된 호환 목적과 명시적 보안 제약 |
| `EXPERIMENTAL` | build 또는 research, production claim 금지 |

## 기준 target

| Target | 등급 | 핵심 경계 |
| --- | --- | --- |
| x86_64 UEFI application boot manager | `PRIMARY` | UEFI consumer와 x86_64 transfer |
| AArch64 UEFI application boot manager | `PRIMARY` | UEFI consumer와 AArch64 normalization |
| AArch64 RPi5 raw-FDT boot manager | `PRIMARY` | RPi image recipe, FDT, fresh live UART는 독립 acceptance |
| AArch64 QEMU virt raw-FDT boot manager | `PRIMARY` | independent QEMU machine target |
| RISC-V 64 SBI boot manager | `STRATEGIC` | S-mode, FDT, SBI |
| RISC-V 64 UEFI application | `STRATEGIC` | UEFI consumer와 RISC-V transfer |
| x86 legacy BIOS client boot manager | `COMPATIBILITY` | EDD, E820, long-mode bridge |
| Ribon UEFI-compatible firmware product | 별도 target별 지정 | firmware provider evidence |
| Ribon BIOS-compatible firmware product | 별도 target별 지정 | firmware provider evidence |

Boot Protocol 지원 등급은 target과 별도로 matrix에 기록한다. LUCA protocol 성공이
Linux, FreeBSD, Multiboot 지원을 의미하지 않는다.

## Acceptance

각 target은 다음을 독립적으로 닫는다.

1. plugin graph와 object graph
2. architecture와 environment 또는 personality unit
3. artifact build와 image recipe 검증
4. 선택된 Boot Protocol의 실제 payload acceptance
5. bounded marker와 raw execution evidence
6. 물리 platform이면 fresh hardware capture

UEFI application QEMU 결과는 Ribon UEFI firmware provider, BIOS, RPi5, RISC-V physical
board 결과를 대신하지 않는다.

RPi5 `PRIMARY`는 유지 우선순위이며 live 성공을 뜻하지 않는다. 자동 acceptance에서 module-bearing
raw-FDT image/package, exact file·module range/hash, canonical signed update manifest와 두 clean-root
재현성은 `package/prehardware` 증거로만 분류한다. 실기기 UART, SD durability, power cycle,
recovery network와 update 실행은 독립적인 live-hardware 증거를 요구한다.

## Firmware consumer와 provider evidence

기존 EDK II 또는 SeaBIOS 위에서 Ribon application을 실행한 결과는 consumer evidence다.
Ribon이 생성한 firmware image가 service ABI와 conformance test를 통과해야 provider
evidence가 된다.

## BIOS 보안 경계

BIOS client가 boot bundle을 검증해도 Ribon 자신에 대한 hardware root of trust는
자동으로 생기지 않는다. TPM measurement, verified firmware, write protection이 없으면
secure-boot production claim을 열지 않는다.

## RISC-V 권한

SBI target에서는 OpenSBI 또는 선택된 SBI firmware가 M-mode를 소유한다. Ribon
bootloader product는 S-mode 또는 UEFI application으로 동작한다. Ribon firmware
personality가 M-mode를 소유하려면 별도 product와 threat model을 요구한다.

RISC-V SBI target의 독립 acceptance는 다음 경계를 모두 확인해야 한다.

- OpenSBI native `a0=hartid`, `a1=FDT` capture
- raw-FDT environment의 `boot_cpu_id`와 machine-description seal
- RLH1 `BOOT_CPU` required section의 bounded producer/parser 검증
- primary OS entry의 `a0=RLH1`, `a1=flags`
- S-mode, interrupt-masked, `satp=0` terminal transfer
- FDT reserve map과 `/reserved-memory`의 firmware-owned normalization
- 선택한 Linux product에서는 2 MiB-aligned Image, `a0=hartid`, `a1=FDT`, PID 1과 clean poweroff

Ribon-owned contract fixture의 QEMU 결과는 이 register와 artifact 경계를 검증할 수
있다. 별도의 Debian Linux product는 QEMU/OpenSBI에서 실제 Linux PID 1 runtime을 검증한다. 어느
결과도 실제 Parus RISC-V consumer, SBI HSM SMP, UEFI RISC-V, 물리 보드 또는 production security
evidence를 대신하지 않는다.
