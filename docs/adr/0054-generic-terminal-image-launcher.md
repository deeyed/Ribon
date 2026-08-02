---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-08-02
code_paths:
  - include/Ribon/boot/terminal.h
  - include/Ribon/service/directory.h
  - include/Ribon/boot/plan.h
  - src/common/boot.c
  - src/environments/uefi-app/terminal_image.c
  - src/protocols/os/linux/efi.c
  - products/bootmgr/manifests/x86_64-uefi-linux.json
tests:
  - make check-terminal-image-launch
  - make check-uefi-managed-image
  - make check-linux-x86_64-efi
  - make check-uefi-product-hermeticity
hardware:
  - none
supersedes:
  - ADR 0053의 firmware-managed provider 미구현 상태
---

# ADR 0054: Generic terminal image launcher

## 결정

Ribon Core는 OS와 firmware에 중립적인 `terminal-image-launch` typed service를 통해
`FIRMWARE_MANAGED_IMAGE` transaction의 마지막 effect를 실행한다. Protocol은 검증된 image의
format·machine·exact source identity, bounded load option과 watchdog budget만 선언한다. Native
firmware handle, system table, boot-services pointer와 device-path pointer는 service provider 밖으로
나오지 않는다.

Service는 product graph에서 명시적으로 선택한다. Managed protocol은 service capability를 요구하지만
handoff·entry-contract capability를 제공하지 않는다. Direct protocol은 기존 handoff, quiesce와
architecture transfer suffix를 유지하며 launcher object를 링크하지 않는다. Core는 protocol이나 OS
stable ID로 terminal 방식을 추론하지 않는다.

## Lifecycle

Managed transaction은 공통 validation과 durable attempt commit 뒤 정확히 한 번 launcher를 호출한다.

```text
SOURCE -> VALIDATED_IMAGE -> POLICY -> COMMIT_ATTEMPT
       -> EXECUTE_TERMINAL(exact request, exact validated source)
       -> child does not return
```

Provider가 반환하면 성공으로 해석하지 않는다. Core는 stable terminal failure receipt를 만들며 같은
transaction에서 재호출을 거부한다. Commit 뒤 failure이므로 attempt state를 되돌렸다고 주장하지
않는다. Direct transaction에서 launcher를 호출하거나 managed transaction을 quiesce/direct-transfer로
보내는 것도 fail-closed다.

## UEFI provider

UEFI application provider는 선택한 file source의 canonical path와 loaded-image device identity가
request와 정확히 일치하는지 다시 검증한다. Provider는 base device path에 bounded file-path node와
end node를 붙여 full device path를 만들고, 같은 immutable byte span을 `LoadImage()`의 source buffer로
전달한다. `LoadImage()`가 만든 child의 loaded-image protocol에서 device handle을 다시 확인한다.

Load option은 `NONE` 또는 bounded UTF-8 command line뿐이다. Provider는 UTF-16 buffer 용량과
종단을 검사해 child loaded image에 연결한다. Watchdog를 요청 budget으로 arm한 뒤 `StartImage()`를
호출한다. Child가 반환하면 watchdog를 해제하고 exit-data를 bounded receipt로 분류한 뒤 child를
`UnloadImage()`하여 실패로 귀결한다. Ribon Core public header에는 EFI native type이 없다.

UEFI 2.11은 `LoadImage()`와 `StartImage()`를 Image Services로 정의하고 image entry에 image handle과
system table을 제공한다. Loaded Image Protocol은 device handle, file path와 load options를 소유한다.
정본은 <https://uefi.org/specs/UEFI/2.11/>,
<https://uefi.org/specs/UEFI/2.11/09_Protocols_EFI_Loaded_Image.html>,
<https://uefi.org/specs/UEFI/2.11/10_Protocols_Device_Path_Protocol.html>이다.

## Linux EFI product

`bootmgr.x86_64-uefi-linux`는 `protocol.linux-efi`, PE/COFF validator와 UEFI launcher를 고정한다.
Build input은 OpenWrt 24.10.0 x86/64 EFI-stub kernel의 URL, size와 SHA-256을 descriptor에 고정하고,
cache가 있어도 다시 검증한다. Initramfs는 source-owned x86_64 PID 1을 deterministic `newc` archive로
만든다. Runtime network는 사용하지 않는다.

Linux EFI stub은 EFI application으로 실행되고 command line의 `initrd=` file path를 firmware
filesystem에서 읽는다. 이 사용법의 정본은 <https://docs.kernel.org/6.1/admin-guide/efi-stub.html>이다.

## Security와 evidence 경계

- Exact source identity, format, machine, executable receipt와 managed execution bit가 다르면 launch 전
  거부한다.
- Path, option, exit-data, size와 offset은 고정 용량과 overflow 검사를 통과해야 한다.
- Launcher는 Ribos helper가 아니다. Policy는 verified intent만 선택하고 Core transaction이 terminal
  effect를 소유한다.
- Linux product는 fixture·external Parus product와 registry, object, link map, ESP와 result root를
  공유하지 않는다.
- QEMU success는 pinned x86_64 OpenWrt EFI-stub, OVMF, Ribon receipt, PID 1 marker와 clean poweroff
  tuple에만 적용한다.

이 ADR은 FreeBSD runtime, physical hardware, production Secure Boot, firmware conformance, 모든 Linux
배포판 또는 returned-child 정상 종료 의미를 열지 않는다.
