---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-09-13
code_paths:
  - make/config.mk
  - make/model.mk
  - make/rules/uefi-bios.mk
  - ports/qemu/virt-aarch64/uefi_port.c
  - products/bootmgr/manifests/aarch64-uefi-parus-fixture.json
  - src/arch/aarch64/arch.c
  - src/environments/uefi-app/uefi_app.c
  - targets/uefi-app/entry.c
  - tools/host/aarch64_uefi_gen.c
tests:
  - make check-aarch64-uefi-host-generation
  - make check-aarch64-uefi-pe
  - make aarch64-uefi-parus-fixture-smoke
  - make check-aarch64-uefi-negative-smoke
hardware:
  - qemu-virt-aarch64-edk2
supersedes:
  - x86-only UEFI application architecture binding
---

# AArch64 UEFI application 계약

`aarch64-uefi-parus-fixture` product는 AArch64 UEFI firmware가 removable-media
경로 `EFI/BOOT/BOOTAA64.EFI`에서 실행하는 Ribon application이다. 이 application은
기존 Ribon Core, `uefi-app` environment, AArch64 architecture backend, ELF64 image
provider와 LUCA protocol을 조합한다. LUCA tree에 별도 EFI loader를 두지 않는다.

## Product와 build authority

Source manifest는 architecture `aarch64`, environment `uefi`, port
`qemu-virt-aarch64`, mode `normal`과 필요한 plugin/service/capability closure를
선언한다. Generated registry, graph report, application object, map, ESP와 실행 결과는
product-owned output root 아래에만 기록한다.

선택 제품 target `aarch64-uefi-parus-fixture`의 dependency graph는 다음 host 생성물을
모두 `tools/host/aarch64_uefi_gen.c`로 만든다.

- canonical plugin registry C와 object graph JSON
- bounded `BOOT.CFG`
- AArch64 ELF64 execution fixture
- opaque initial-image fixture

C host tool은 manifest를 64 KiB, 512 token, depth 8에서 닫고 예상 key, 값, 배열 순서,
service/plugin selection과 resource limits가 정확히 일치하지 않으면 출력하지 않는다.
Registry와 graph serialization은 기존 `generate_plugin_registry.py`의 canonical 출력을
byte-for-byte 보존한다. Python 생성기는 differential test의 reference일 뿐 선택 제품
target의 build prerequisite나 recipe가 아니다.

Compiler와 linker executable은 caller가 `AARCH64_UEFI_CC`와
`AARCH64_UEFI_LLD_LINK`로 결속한다. Source나 recipe는 설치 prefix를 추측하지 않는다.
Target compile은 ARM64 COFF와 AArch64 UEFI header를 선택하고 link는 machine ARM64,
EFI application subsystem, `efi_main` entry, dynamic base와 base relocation을 요구한다.

## Firmware entry와 lifetime

Firmware는 AArch64 UEFI application ABI로 image handle과 System Table을 전달하고
16-byte aligned stack을 제공한다. Shared `targets/uefi-app/entry.c`는 compile-time ISA를
Ribon target architecture와 대조한 다음 native values를 `uefi-app`에 한 번 제공한다.
Architecture backend는 CPU cache, privilege와 final register transfer만 소유한다.

`uefi-app`은 loaded-image device, file protocol, memory map key, page allocation과
`ExitBootServices()`를 private context에 유지한다. Generic Core와 protocol은 raw UEFI
pointer를 저장하지 않고 typed boot source, memory observation, persistent input과 service
descriptor만 받는다. Final memory-map refresh와 bounded `ExitBootServices()`가 성공한
뒤에는 firmware service를 다시 호출하지 않는다. Environment quiesce, cache sync와
architecture transfer 순서는 바뀌지 않는다.

`qemu-virt-aarch64` port는 UEFI environment identity와 AArch64 identity를 함께 선언하고
PL011 diagnostic sink 및 selected payload placement window만 제공한다. 같은 common port
header를 사용하는 raw-FDT product의 environment identity는 계속 raw-FDT이며 runtime
fallback이나 first-success selection은 없다.

## Failure와 진단

Serial evidence는 entry 뒤 첫 실패 stage를 정확히 한 번 기록한다. Malformed config와
missing payload는 `esp-config`, missing initial image는 environment file-service의
`init-image-load`, malformed ELF는 `boot-prepare`에서 닫힌다. 이 실패들은
`RIBON-R4-UEFI-TRANSFER`를 기록하지 않는다. Malformed PE/COFF는 host binary validator가
ARM64 machine, PE32+, EFI application subsystem, nonzero entry, `.text`와 `.reloc` 요구로
거부한다.

정상 QEMU smoke는 firmware entry, config, initial image, memory map, product graph,
protocol preparation, payload placement, final map, `ExitBootServices`, architecture transfer와
AArch64 fixture entry를 순서대로 한 번씩 요구한다. Harness는 timeout 뒤 process group을
정리하고 firmware, payload, manifest, ESP, raw serial과 QEMU identity digest를 보존한다.

## Evidence 경계

Host generation과 PE parser 결과는 unit/compile-link evidence다. Selected EDK2와
QEMU `virt`에서 BOOTAA64가 실행되고 PL011 stage marker와 fixture entry가 관찰된 결과만
qemu-runtime evidence다. 이 계약은 Ribon EFI application 실행을 닫지만 LUCA kernel
handoff, World 시작, UTM GUI, Secure Boot, rollback protection, 실제 board firmware 또는
physical hardware 실행을 주장하지 않는다.
