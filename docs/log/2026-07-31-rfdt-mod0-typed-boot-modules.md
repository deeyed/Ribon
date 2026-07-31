---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-07-31
code_paths:
  - include/Ribon/boot/module_bundle.h
  - src/common/module_bundle.c
  - src/environments/raw-fdt/
  - products/bootmgr/raw_fdt_main.c
  - tools/generate_boot_module_bundle.py
  - tools/qemu_target_smoke.py
  - tools/package_rpi5.py
  - tools/check_rpi_package.py
tests:
  - make check-boot-modules
  - make check-rph1
  - make qemu-aarch64-virt-modules-fixture-smoke
  - make qemu-aarch64-virt-parus-modules-smoke
  - make qemu-aarch64-virt-raw-fdt-smoke
  - make qemu-riscv64-virt-rph1-fixture-smoke
  - make x86_64-uefi-parus-fixture-smoke
  - make rpi5-aarch64-parus-modules-package
  - make rpi5-aarch64-modules-fixture-package
  - make check-target-builds
  - make qstar-check
  - make check
  - make docs
hardware:
  - not-run
supersedes:
  - none
---

# RFDT-MOD0 typed boot-module 구현 기록

## 구현

RFDT-MOD0는 raw-FDT boot manager에 0..8개의 typed boot module publication을 추가했다.
Build-time generator는 exact component input을 product-owned root로 snapshot하고 page-aligned
`.ribon.boot_modules` assembly, typed service descriptor와 provenance JSON을 생성한다. Runtime은
product capability/service authority, linker section, exact/module backing range와 overlap을
검사한 뒤 `RibonBootEnvironmentPersistentInputs`에 module inventory를 보존한다.

Module-aware QEMU/RPi5 fixture와 external-Parus product를 module-free product와 분리했다.
Module-free raw-FDT link map에는 module support/generated object가 없고 environment 의미도
유지된다. RPH1 wire format은 변경하지 않았으며 기존 `BOOT_MODULES` builder/parser의
malformed corpus를 확장했다.

RPi5 package schema v2는 `kernel8.img` 안 module offset, exact size, page backing, physical
address와 digest를 provenance 순서에 결합한다. Copied product manifest, product digest와
bundle digest도 package manifest가 다시 결합하고 checker가 exact authority와 bytes에서
재검증한다. Package v1은 module-free product에 유지된다.

## 실행 증거

실제 AArch64 QEMU 입력은 다음 immutable artifact였다.

- Parus RPH1 kernel ELF SHA-256:
  `3556d61da14429017691253aebcd3925be660ebf96cde42c9a0b1948334b3ce7`
- External initial-image ELF SHA-256:
  `523b22d16244368bb007b48df4dec742cd2b30c943443339ca58c6d30d234351`

Ribon raw-FDT는 module count 1, initial-image count 1을 기록했다. Parus consumer는
`EXTERNAL_INITIAL_USER` receipt의 `ROLE=INITIAL_IMAGE`와 `MODULES=1:RESULT=OK`를 기록했고,
entry, stage0, xibalba, EB0..EB9, kernel main과 idle marker가 순서대로 관찰됐다. Result와 raw
serial은 다음에 보존했다.

```text
build/results/qemu-aarch64-virt-parus-modules.json
build/results/qemu-aarch64-virt-parus-modules.log
build/targets/qemu-aarch64-virt-parus-modules/results/boot-modules.json
```

RPi5 package는 RPi5 RPH1 ELF
`5b5ff99d1dd1e5cbd7500b7717362f66a4c92a34144b7cda4fcfc8437c2a6bb9`와 같은 initial-image
component를 사용해 package v2 checker를 통과했다.

```text
build/targets/rpi5-aarch64-parus-modules/package/manifest.json
build/targets/rpi5-aarch64-parus-modules/package/metadata/boot-modules.json
```

8-module upper-bound fixture QEMU, module-free AArch64 raw-FDT, RISC-V OpenSBI RPH1 fixture와
x86_64 UEFI module path도 각각 fresh smoke를 통과했다. Host tests는 duplicate initial image, 9번째 module,
zero/wrapping/overlap, malformed authority, short/corrupt component, deterministic digest,
23-region success와 22-region capacity failure를 검사했다. RPi5 external product는 실패한
payload validation JSON을 다음 Make 실행의 성공 prerequisite로 재사용하지 않고 매번 payload를
다시 검증하며, 실패 결과를 성공처럼 바꾼 뒤 재시도하는 regression도 fail-closed를 확인한다.

## 증거 경계

열 수 있는 구현 claim은 raw-FDT product가 typed boot module을 기존 RPH1 consumer에 전달한다는
것까지다. 이 기록은 physical RPi5 실행, production secure boot, OTA/update authority 또는
Ribon이 전달한 module을 이용한 user-process 기능의 제품 수준 성공을 주장하지 않는다.
Parus source와 Parus gitlink는 이 작업에서 수정하지 않았다.
