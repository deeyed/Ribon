---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-08-03
code_paths:
  - src/image-formats/linux_riscv64.c
  - src/protocols/os/linux/
  - src/common/sys/fdt/
  - src/environments/raw-fdt/
  - products/bootmgr/manifests/qemu-riscv64-virt-linux.json
  - tools/prepare_external_linux_riscv64_image.py
  - tools/check_multi_os_runtime.py
  - external/inputs/linux-riscv64-debian-13-installer-20250803-deb13u6.json
tests:
  - make check-linux-riscv64
  - make check-multi-os-runtime
  - make check-target-builds
hardware:
  - qemu-riscv64-virt-opensbi
  - physical-riscv-not-run
supersedes:
  - ADR 0052의 RISC-V Linux 선택 금지
  - RISC-V SBI target의 protocol-fixture-only evidence state
---

# ADR 0056: Linux RISC-V64 raw Image와 multi-OS evidence

## 결정

RISC-V64 Linux raw `Image`는 `image.linux-riscv64`라는 독립 format plugin으로 검증한다. Parser는
64-byte little-endian header, 두 magic, header version 2, little-endian flag, reserved field,
optional PE signature와 effective image size를 bounded하게 검사한다. Image는 최대 64 MiB이며
product placement는 RV64 Linux 계약에 따라 2 MiB 정렬을 요구한다. Parser는 board 주소나 OpenSBI
machine ID를 소유하지 않는다.

`protocol.linux`의 FDT handoff 의미는 AArch64와 RISC-V가 공유한다. Terminal register route만
architecture별로 생성한다. RISC-V route는 environment가 검증한 bootstrap hart authority를 요구하고
`a0=hartid`, `a1=FDT`, argument count 2, translation disabled를 선언한다. Architecture backend가
S-mode terminal transfer에서 interrupt를 mask하고 `satp=0` 상태를 보장한다.

Raw-FDT normalization은 header reserve map과 active `/reserved-memory` direct child의 bounded
`reg` tuple을 모두 firmware-owned range로 가져온다. FDT blob 자체도 별도 firmware range다.
`status = "disabled"` child는 property 순서와 무관하게 range authority에서 제외한다. Active child의
zero-size, wrapping, partial memory-bank overlap, 중복 또는 capacity 초과는 transfer 전에 거부한다.
이 규칙은 OpenSBI나 RPi5 전용 분기가 아니라 모든 raw-FDT product에 적용된다.

외부 kernel은 Debian 13 installer RISC-V64 artifact의 versioned URL, exact size와 SHA-256를 tracked
descriptor로 고정한다. Binary는 저장소에 넣지 않고 product-scoped build cache에서 매번 재검증한다.
Source-owned static PID 1은 deterministic initramfs와 typed auxiliary module로 조합한다.

`check-multi-os-runtime`은 네 실제 OS tuple과 세 Ribon-owned Parus protocol fixture를 같은 source
revision에서 재실행한다. 각 row는 `qemu-runtime` 또는 `qemu-contract-fixture`를 유지하며 하나의
support boolean로 축약하지 않는다. Payload, product, composed artifact, firmware, external validation,
raw serial과 result hash를 독립 보존한다.

## 권한 경계

- Core는 Linux, OpenSBI, Debian 또는 QEMU 이름을 분기하지 않는다.
- Image plugin은 형식과 extent를, protocol은 Linux entry 의미를, product/port는 placement와 target
  service를 소유한다.
- Firmware reservation은 OS protocol이 아니라 raw-FDT environment가 정규화한다.
- Parus fixture row는 RPH1/register 회귀만 증명하며 현재 Parus kernel runtime boot를 증명하지 않는다.

## 실패 정책

Truncated 또는 oversized Image, wrong magic/version/endianness, zero/wrapping extent, misaligned product
placement, bootstrap hart 누락, FDT 누락, reserved range overflow/overlap, initramfs 비예약·중첩과
matrix row의 revision/hash/cleanup 불일치는 fail-closed한다. 한 row 실패를 다른 OS 성공으로 덮지 않는다.

## 비주장

이 결정은 Debian installer kernel을 사용하는 QEMU `virt` + OpenSBI 단일-hart tuple에 한정된다.
물리 RISC-V board, SBI HSM SMP, RISC-V UEFI, FreeBSD RISC-V, 모든 Linux 배포판, production secure boot,
production firmware와 현재 Parus kernel의 RISC-V runtime 성공은 주장하지 않는다.
