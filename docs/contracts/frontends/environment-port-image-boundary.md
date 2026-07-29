---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-29
code_paths:
  - src/environments/
  - src/image-formats/
  - src/arch/
  - ports/
  - products/bootmgr/
  - targets/
tests:
  - make check-frontends
  - make check-target-builds
  - make qemu-aarch64-virt-raw-fdt-smoke
  - make x86_64-uefi-app-smoke
hardware:
  - none
supersedes:
  - monolithic boot frontend ownership
---

# Environment·Port·Image 경계 계약

Boot target은 native 실행환경, machine resource, executable parser와 OS protocol을 서로
다른 component로 결합한다. 한 component는 다른 축의 identity나 policy를 추론하지
않는다.

## 정확한 target tuple

Boot manager product는 다음 tuple을 source manifest에 선언한다.

```text
architecture × environment × typed port services × image format × boot protocol × mode
```

각 product는 architecture와 environment provider를 정확히 하나씩 포함하고 필요한
port service role만 선언한다. Generated registry, service directory와 final link map이
이 선택을 검증한다. Directory scan, weak fallback, board alias, runtime probe로 다른
provider를 선택하지 않는다.

## Environment consumer

Environment는 native input을 firmware-neutral `RibonBootEnvironment`와
typed `RibonServiceDescriptor` 집합으로 변환한다. QStar가 해당 집합을 product-owned
immutable `RibonServiceDirectory`에 연결한다.

| Environment | 소유하는 native 계약 |
| --- | --- |
| `host` | caller가 제공한 file, clock와 synthetic memory facts |
| `uefi-app` | Boot Services memory map, page allocation, monotonic count, loaded-image Simple File System/Block I/O, ExitBootServices |
| `bios-client` | E820, EDD와 long-mode 진입 전 native callback |
| `raw-fdt` | entry register의 FDT pointer와 bounded FDT blob |

Environment source는 RPH1, Parus entry flag, RPi5, QEMU identity를 알지 않는다.
Environment가 제공한 borrowed native pointer는 generic Core 또는 Protocol에 저장하지
않는다.

UEFI final transaction은 memory map capture, handoff refresh, `ExitBootServices`를
bounded retry로 묶는다. 성공 뒤 Boot Services callback을 호출하지 않는다.
Target이 선택한 boot media, command line과 typed boot module inventory는
`RibonBootEnvironmentPersistentInputs`로 분리한다. 최초 capture와 모든 retry capture는
같은 persistent input을 적용한 뒤에만 protocol handoff를 재생성한다.

UEFI consumer는 loaded-image device의 native file and block handle을 environment-private context에
유지하고 canonical read-only file source 또는 `RibonReadOnlyBlockDevice`로만 변환한다. File size와
exact read를 검증하고, configuration-selected payload path가 아닌 build-embedded bytes를 external
media product의 source로 사용하지 않는다.

Raw-FDT parser는 allocation과 MMIO 없이 header, structure, string bounds를 검증한다.
Product와 machine-description service가 선언한 native input 상한을 넘는 blob과 memory
reservation 충돌을 거부한다.

## Typed port services

Port는 machine wiring을 아래처럼 독립 service descriptor로 제공한다.

- diagnostic sink의 initialize/write operation과 polling 상한
- machine-description input 주소, 크기와 format
- payload-placement의 허용 physical window와 alignment

Core와 Boot Library는 stable board ID나 monolithic fact table을 요구하지 않는다.
Standard firmware가 모든 resource authority를 제공하면 port service 없이도 product를
조합할 수 있다. Board source는 OS handoff, image parser, boot policy를 포함하지
않는다. QEMU `virt`와 RPi5는 AArch64, raw-FDT, FDT parser, PL011 driver를 공유하지만
port object, linker, artifact, package와 evidence marker를 공유하지 않는다.

## Image format

ELF64와 PE/COFF parser는 `RibonImageFormatOps`를 구현한다. Parser는 caller-owned segment
array에 bounds-checked load plan을 만들며 memory allocation, firmware page placement,
register ABI를 수행하지 않는다. Parser는 machine field를 추출하지만 architecture
descriptor를 소비하거나 ISA 이름을 비교하지 않는다. Machine 일치, canonical virtual
address와 address-width 검증은 architecture backend만 소유한다.

PE/COFF consumer는 PE32+ 구조와 machine field를 추출하고 import 또는 relocation을
해결하지 않는다. Preferred image base에 배치할 수 없는 product는 별도 relocation
capability 없이는 해당 image를 거부한다.

## Architecture

Architecture backend는 CPU instruction과 register transfer만 소유한다. Architecture
source에 Parus, RPi5, QEMU 또는 firmware product policy를 두지 않는다. Counter read,
cache sync, privilege normalization, terminal transfer는 capability와 callback 존재가
정확히 일치해야 한다.

`RibonArchDescriptor.elf_machine`과 `pe_coff_machine`은 format별 ISA fact다.
`pe_coff_machine` 0은 해당 ISA에서 그 format을 지원하지 않음을 뜻한다. Architecture
validator는 parser가 추출한 machine, entry와 segment address를 이 fact 및 address width에
대해 검증한다.

## Evidence

다음 evidence class는 독립적이다.

- parser와 descriptor `unit`
- BIOS environment `compile-only`
- UEFI application과 raw-FDT `qemu-runtime`
- RPi5 image header, selected object와 package digest `package`
- RPi5 UART `hardware`

QEMU 또는 package 결과를 physical RPi5 실행으로 승격하지 않는다.
