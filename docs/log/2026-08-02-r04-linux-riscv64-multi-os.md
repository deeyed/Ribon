---
doc_type: devlog
status: historical
authority: non-normative
last_verified: 2026-08-02
code_paths:
  - src/image-formats/linux_riscv64.c
  - src/protocols/os/linux/protocol.c
  - src/common/sys/fdt/fdt.c
  - products/bootmgr/manifests/qemu-riscv64-virt-linux.json
  - tools/check_multi_os_runtime.py
tests:
  - make check-linux-riscv64
  - make check-multi-os-runtime
  - make check-target-builds
hardware:
  - qemu-only
  - physical-hardware-not-run
supersedes:
  - none
---

# R04 Linux RISC-V64와 multi-OS evidence closure

## 구현

- Linux RISC-V64 Image를 별도 format capability와 product-scoped parser로 추가했다.
- Linux protocol의 공통 FDT handoff는 유지하고 RISC-V register ABI와 bootstrap hart authority만
  typed route로 분리했다.
- FDT reserve map과 `/reserved-memory` child를 bounded reservation fact로 가져와 raw-FDT normalized
  memory map에 `FIRMWARE`로 투영했다.
- Debian 13 installer RISC-V64 kernel을 versioned URL, exact size와 SHA-256로 고정하고 source-owned
  static PID 1 initramfs를 typed module로 조합했다.
- 네 OS runtime과 세 protocol fixture의 immutable result를 검사하는 multi-OS matrix를 추가했다.

## 실행 증거

QEMU 11.0.2 `virt` + OpenSBI 1.7에서 Linux 6.12.94+deb13-riscv64가 실행되었다. Ribon은 OpenSBI가
기술한 두 `mmode_resv` range와 FDT blob을 세 firmware region으로 정규화했다. Linux는 `/init`을
실행해 `RIBON:LINUX:PID1:v1:OK`를 한 번 출력하고 `reboot: Power down`으로 종료했다. Result는
payload SHA-256 `c601b3ef8415fb0309c5098569cab61954916a9388fba929a32e11f024e8490a`, OpenSBI hash,
Ribon image, initramfs provenance, exact command, raw serial hash, clean exit와 forced kill false를 보존한다.

동일 실행에서 multi-OS matrix는 다음을 닫았다.

- `qemu-runtime` 4개: Linux AArch64 raw-FDT, Linux x86_64 EFI stub, FreeBSD amd64 UEFI,
  Linux RISC-V64 OpenSBI
- `qemu-contract-fixture` 3개: Parus protocol AArch64, x86_64, RISC-V64
- physical hardware: `not-run`
- production firmware: `not-claimed`

FreeBSD row는 single-user terminal prompt 관측이며 guest clean poweroff가 아니다. Parus fixture row는
RPH1/register 회귀만 증명하고 현재 Parus kernel runtime을 증명하지 않는다. RPi5와 물리 RISC-V board,
production secure boot, SBI HSM SMP와 장시간 안정성은 실행하지 않았다.

## 외부 Parus payload 진단

Ribon tree 밖의 당시 Parus build output도 읽기 전용 입력으로 진단했지만 multi-OS matrix claim에는
포함하지 않았다. AArch64 RPi5 package payload는 QEMU AArch64 product window용 artifact가 아니어서
external-input preflight에서 거부되었다. x86_64 payload는 Ribon transfer와 Parus EB0–EB2 뒤
`EB3:FAIL:MMU_POLICY_FAIL`로 종료되었다. 이는 현재 Parus artifact와 product tuple의 별도 integration
상태이며, Ribon-owned fixture 회귀 성공으로 덮거나 Ribon의 actual Parus runtime 성공으로 기록하지
않는다.
