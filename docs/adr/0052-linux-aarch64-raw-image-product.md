---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-08-02
code_paths:
  - src/image-formats/linux_aarch64.c
  - src/protocols/os/linux/
  - products/bootmgr/manifests/qemu-aarch64-virt-linux.json
  - tools/prepare_external_linux_image.py
  - external/inputs/linux-aarch64-openwrt-23.05.3.json
tests:
  - make check-linux-boot
  - make check-linux-external-input
  - make qemu-aarch64-virt-linux-smoke
hardware:
  - qemu-aarch64-virt
  - rpi5-not-run
supersedes:
  - Linux protocol as descriptor-only experimental support
---

# ADR 0052: Linux AArch64 raw Image product

## 결정

AArch64 Linux raw `Image`를 ELF로 위장하지 않는다. `image.linux-aarch64`는 64-byte Linux Image
header를 검사하고 product payload-placement에 상대적인 단일 segment plan을 만든다. Generic Boot
Library는 `RELOCATABLE` plan을 selected placement service로 한 번 재배치한다. Board 주소를 image
parser나 OS protocol에 넣지 않는다.

Linux protocol은 AArch64 raw-FDT tuple만 선택하며 x0에 compact FDT를 전달한다. Typed auxiliary
module 이름 `initramfs`를 최대 하나 허용하고, exact BOOT_MODULE reservation과 kernel 비중첩을 확인한
뒤 `/chosen` initrd property를 생성한다. RISC-V Linux 의미는 별도의 executable evidence와 ABI 결정이
생기기 전까지 선택하지 않는다.

Pinned external kernel은 source tree에 commit하지 않는다. Tracked descriptor가 upstream identity,
license, size, hash와 class를 소유하고, build cache는 매번 재검증한다. QEMU는 kernel bytes를 placement
주소에 별도 pre-load한다. Ribon binary에는 generated address/size descriptor만 링크한다.

## 이유

이 분리는 세 authority를 보존한다.

- image plugin: raw Image class와 memory extent
- product/port: physical placement window
- Linux protocol: FDT entry와 initramfs 의미

따라서 raw-FDT core에 OS 분기가 생기지 않고, external binary를 source artifact처럼 커밋하지 않으면서도
재현 가능한 identity와 QEMU evidence를 남길 수 있다.

## 실패 정책

Wrong architecture/class/digest, zero 또는 oversized Image, missing/malformed FDT, duplicate initrd
property, initramfs/kernel overlap, BOOT_MODULE 미예약, 32 GiB initrd window 위반과 handoff capacity
초과는 transfer 전에 fail-closed한다.

## 비주장

이 결정은 AArch64 QEMU virt의 pinned OpenWrt kernel과 source-built PID 1만 검증한다. Linux 배포판
일반 호환성, RISC-V Linux, EFI stub, RPi5 실기기, secure boot, production key와 production firmware
지원은 주장하지 않는다.
