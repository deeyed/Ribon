---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - include/Ribon/boot/module_bundle.h
  - src/common/module_bundle.c
  - products/bootmgr/raw_fdt_main.c
  - tools/generate_boot_module_bundle.py
  - products/bootmgr/manifests/
tests:
  - make check-boot-modules
  - make check-target-builds
  - make qemu-aarch64-virt-parus-modules-smoke
hardware:
  - none
supersedes:
  - raw-FDT embedded-kernel-only component model
---

# ADR: raw-FDT module은 build-time bundle과 typed publication으로 분리한다

## 맥락

UEFI frontend는 filesystem에서 initial image와 auxiliary module을 exact-size page backing으로
읽고 persistent environment input으로 publication할 수 있다. raw-FDT product에는 filesystem
service가 없고 embedded kernel payload만 존재했다. RPH1 builder와 consumer는 이미 typed
`BOOT_MODULES`를 지원하므로 이 차이를 새 wire protocol이나 OS 전용 fallback으로 메우면 같은
semantic artifact를 두 경로에서 다르게 조립하게 된다.

## 결정

1. Source-neutral component manifest는 stable logical name, semantic role, relative source,
   exact size, maximum size와 SHA-256를 선언한다.
2. Host generator는 immutable input snapshot, architecture-neutral ELF assembly, typed C
   descriptor와 product-bound provenance를 product build root에 생성한다.
3. Module bytes는 bootloader runtime `.rodata`가 아니라 canonical
   `.ribon.boot_modules` loadable section에 page-aligned slot으로 둔다.
4. Generated bundle은 persistent data-only service다. Product manifest의 exact service와
   required/allowed capability가 동시에 존재할 때만 raw-FDT entry가 소비한다.
5. Runtime materializer는 exact file span과 page backing reservation을 구분하고 bootloader,
   kernel 및 module overlap을 fail-closed한다.
6. Generic environment가 typed module list를 소유하고 selected Boot Protocol만 wire format으로
   투영한다. Parus는 기존 RPH1 `BOOT_MODULES`를 그대로 사용한다.
7. Module-free product는 service, capability와 module objects를 링크하지 않으며 이전 의미를
   유지한다.

## 결과

같은 architecture-neutral raw-FDT mechanism을 AArch64 QEMU와 RPi5 module product가 사용하며,
schema에는 board 또는 OS 이름이 들어가지 않는다. RISC-V raw-FDT linker도 같은 canonical
section-symbol contract를 구현하지만 module-bearing RISC-V product와 runtime evidence는 이 결정의
완료 claim에 포함하지 않는다. Product graph가 module authority를 명시하므로 외부 Make input만으로
module publication을 활성화할 수 없다. Exact bytes와 provenance가 linker, QEMU evidence와 RPi5
package manifest에 연결된다.

Bundle은 build-time component를 다루므로 runtime filesystem이나 allocation을 요구하지 않는다.
반면 module을 바꾸면 target image를 다시 link/package해야 한다. Runtime storage/network
discovery와 OTA-selected module은 별도 boot-source 및 update authority 계약이 필요하다.

## 기각한 대안

- UEFI file loader를 raw-FDT에 복제하는 방식은 filesystem이 없는 environment에 UEFI 수명과
  API를 새로 만들게 되므로 기각한다.
- Kernel payload와 module을 하나의 embedded semantic artifact로 합치는 방식은 role, memory
  reservation과 protocol component budget을 잃으므로 기각한다.
- `raw_fdt_main.c`의 board 또는 Parus 조건 분기는 generic frontend 경계를 깨므로 기각한다.
- Module bytes를 bootloader reservation 안의 borrowed pointer로만 넘기는 방식은 OS가 해당
  range를 reclaim할 수 있고 backing lifetime을 표현하지 못하므로 기각한다.
- Component input 존재만으로 generated object를 링크하는 방식은 product graph authority를
  우회하므로 기각한다.
