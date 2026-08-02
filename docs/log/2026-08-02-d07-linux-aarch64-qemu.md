---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-08-02
code_paths:
  - src/image-formats/linux_aarch64.c
  - src/protocols/os/linux/
  - products/bootmgr/manifests/qemu-aarch64-virt-linux.json
  - tests/fixtures/linux/aarch64/
tests:
  - make check-linux-boot
  - make check-linux-external-input
  - make qemu-aarch64-virt-linux-smoke
hardware:
  - qemu-aarch64-virt
  - rpi5-not-run
supersedes:
  - none
---

# D07 AArch64 Linux QEMU boot 실행 기록

## 구현

Linux AArch64 raw Image parser와 relocatable placement, compact FDT builder, typed initramfs publication,
pinned external-input validator와 deterministic source-built PID 1 archive를 추가했다. Linux 관련 의미는
image/protocol/product package에만 있고 generic raw-FDT frontend는 typed module과 selected descriptors만
소비한다.

External input은 OpenWrt 23.05.3 `armsr/armv8` generic kernel이며 size는 16,452,096 byte, SHA-256는
`cc281030454415267654a53c0d85f7bea79846258f1409bacfdf814d40ffede1`이다. Binary는 build cache에만
존재하며 source tree에 포함하지 않는다.

## 실행 evidence

QEMU AArch64 virt에서 raw-FDT capture, product graph, protocol handoff, 0x41000000 placement와 transfer가
순서대로 완료됐다. Linux 5.15.150은 generated initramfs를 unpack하고 `/init`을 실행했다. PID 1은
`RIBON:LINUX:PID1:v1:OK`를 한 번 기록한 뒤 poweroff syscall을 호출했고 Linux는 `reboot: Power down`을
기록했다. QEMU는 status 0으로 자체 종료했으며 forced kill은 0이다.

Raw serial과 result JSON은 `build/results/qemu-aarch64-virt-linux.*`에, product graph, module provenance와
external validation은 `build/targets/qemu-aarch64-virt-linux/results/`에 보존한다. Build output은 commit하지
않는다.

## Negative evidence와 비주장

Host corpus는 wrong digest/class/architecture, oversize, missing FDT, BOOT_MODULE 미예약,
kernel/initramfs overlap, 작은 handoff buffer와 invalid raw Image를 거부한다. 이 라운드는 QEMU evidence다.
출장 중이므로 RPi5 live 실행을 하지 않았고 physical hardware, RISC-V Linux, production secure boot와
배포판 일반 호환성을 주장하지 않는다.
