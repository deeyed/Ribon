---
doc_type: devlog
status: accepted
authority: evidence
last_verified: 2026-07-26
code_paths:
  - include/Ribon/platform/
  - src/common/
  - src/environments/
  - src/image-formats/
  - platforms/
  - products/bootmgr/
  - targets/
tests:
  - make check
  - make check-target-builds
  - make qemu-aarch64-virt-raw-fdt-smoke
  - make x86_64-uefi-app-smoke
  - make docs
hardware:
  - none
supersedes:
  - none
---

# R4 Environment와 Boot Target 재구성 기록

## 범위

R4는 board-specific entry와 firmware consumer를 공용 boot directory에서 제거하고
architecture, environment, platform, image format, Boot Protocol을 product manifest로
조합했다. Linux와 FreeBSD protocol, RISC-V SBI, firmware provider personality,
network/update 기능은 이 기록의 구현 범위가 아니다.

## 구현

- `src/boot`를 제거하고 native entry, linker, package recipe를 `targets/`로 이동했다.
- UEFI application, BIOS client, raw-FDT, host를 독립 Environment consumer로 분리했다.
- QEMU AArch64 virt, RPi5, PC UEFI, PC BIOS resource를 `platforms/` provider로 분리했다.
- FDT parser와 PL011 driver를 board-neutral common component로 만들었다.
- ELF64와 PE/COFF parser를 `src/image-formats/`에 분리하고 공통 image ABI validator는
  `libribon-boot`가 소유한다.
- Product JSON manifest는 architecture, environment, platform을 정확히 하나씩
  선택하며 generated registry와 selected-object report를 만든다.
- x86_64 UEFI target은 final memory map capture 뒤 RPH1을 다시 생성하고 bounded
  `ExitBootServices` transaction을 수행한다.
- AArch64 raw-FDT target은 QEMU와 RPi5에서 environment와 product source를 공유하되
  platform, entry, linker, artifact와 package를 분리한다.

## 검증 해석

`make check`는 ELF64, PE/COFF, FDT, RPH1 unit과 세 architecture host product,
capability graph, archive object graph, hard-cut lint, QStar closure를 실행했다.

`make qemu-aarch64-virt-raw-fdt-smoke`는 QEMU virt에서 FDT capture, product graph,
RPH1, payload load, AArch64 register ABI와 fixture entry를 확인했다.

`make x86_64-uefi-app-smoke`는 EDK II QEMU firmware를 소비하여 UEFI entry, payload page
placement, final memory map, RPH1 refresh, ExitBootServices, x86_64 register ABI와 fixture
entry를 확인했다. 이는 UEFI consumer evidence이며 Ribon firmware provider evidence가
아니다.

BIOS는 environment와 native contract의 `compile-only` 증거만 생성했다. RPi5는 image,
Linux arm64 image header, selected object graph, file digest와 package manifest를
검사했다. RPi5 실기 UART 실행은 수행하지 않았다.
