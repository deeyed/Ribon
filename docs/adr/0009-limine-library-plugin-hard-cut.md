---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-26
code_paths:
  - include/Ribon/
  - src/common/
  - src/arch/
  - src/environments/
  - src/protocols/
  - src/plugins/
  - src/firmware/
  - products/
  - targets/
tests:
  - ribon-library-boundary-lint
  - ribon-plugin-graph-lint
  - ribon-product-composition-test
hardware:
  - none
supersedes:
  - 0001-legacy-os-semantic-hard-cut
  - 0002-core-profile-platform-boundary
  - 0003-parus-handoff-v1 ownership placement
  - 0004-kernel-owned-higher-half ownership placement
  - 0005-split-ota-authority product placement
  - 0006-reboot-time-overseer product placement
  - 0007-bounded-recovery-network product placement
  - 0008-bios-riscv-support-tiers frontend placement
---

# ADR: Limine식 Library·Plugin·Protocol 구조로 hard cut한다

## 맥락

Ribon의 Core, architecture operation, mode capability는 generic 경계를 지향하지만 실제
frontend와 build graph는 Parus profile, UEFI application, RPi5, QEMU `virt`, image
packaging을 직접 결합한다. `Profile` 이름은 OS protocol과 product policy를 함께
표현하고, `FirmwareAdapter`는 기존 firmware를 소비하는 역할과 firmware를 제공하는
역할을 구분하지 않는다.

Ribon은 Parus 전용 bootloader가 아니라 Linux, FreeBSD, Multiboot, chainload 같은
protocol을 독립적으로 지원할 generic bootloader이자 embeddable library여야 한다.
또한 외부 module을 조합해 BIOS·UEFI-compatible firmware를 개발할 SDK 경계를 제공해야
한다.

Limine의 common runtime, OS protocol, BIOS/UEFI target 분리는 이 목표의 기준이 된다.
EDK II의 module, package, platform, image metadata는 firmware SDK 조합 모델의 기준이
된다.

## 결정

Ribon은 하위호환 없이 다음 구조로 전환한다.

1. `Profile`을 삭제하고 `Boot Protocol`로 대체한다.
2. Core와 Boot 기능을 `libribon-core`, `libribon-boot`로 분리한다.
3. Plugin은 build-time에 선택하고 QStar가 immutable registry를 생성한다.
4. Architecture, environment consumer, platform, protocol, driver, policy, firmware
   personality를 서로 다른 plugin kind로 둔다.
5. UEFI/BIOS consumer와 UEFI/BIOS provider를 반대 방향의 component로 구분한다.
6. Product와 target manifest가 최종 object graph와 image recipe를 소유한다.
7. Parus Handoff는 Parus protocol이 소유한다.
8. Parus overseer와 health/update policy는 OS-specific policy plugin 또는 companion
   package가 소유한다.
9. Runtime-loadable plugin은 별도 보안 계약 전에는 지원하지 않는다.

이전 `RibonProfile`, `RibonFirmwareAdapter`, builtin profile registry, monolithic
`rpi_main`과 `uefi_main`, `src/boot` ownership을 위한 wrapper와 alias를 두지 않는다.

## 유지하는 불변식

이 ADR은 이전 ADR의 placement를 supersede하지만 다음 안전 원칙을 유지한다.

- Parus RPH1 wire ABI는 Parus protocol 밖으로 나오지 않는다.
- Permanent OS higher-half는 OS가 소유한다.
- Normal boot는 network 성공에 의존하지 않는다.
- Update writer와 recovery network는 normal product에 링크하지 않는다.
- Boot confirmation은 generation과 nonce replay를 방지한다.
- Ribon bootloader는 OS 실행 중 상주하는 hypervisor가 아니다.
- QEMU, package, firmware consumer, physical hardware evidence를 구분한다.

## 기각한 대안

### 기존 Profile의 이름만 Protocol로 변경

Frontend의 직접 Parus 호출과 Core archive의 OS object가 유지되므로 선택하지 않는다.

### Compatibility shim

두 public ABI와 두 registry가 동시에 유지되어 object graph와 failure semantics가
분기되므로 선택하지 않는다.

### 모든 기능을 동적 plugin으로 전환

Preboot executable loading, signature, relocation, W^X, dependency authenticity와 crash
containment가 먼저 필요하므로 선택하지 않는다.

### EDK II 전체 구조 복제

Ribon의 작은 결정론적 Core보다 넓은 phase와 protocol database를 기본 요구하게 되므로
선택하지 않는다. Firmware personality 안에서 필요한 호환 surface만 구현한다.

### Limine 파일 suffix와 build scan을 그대로 사용

Architecture와 environment 선택을 명확히 보여주지만 external plugin SDK와 build-time
dependency 검증에는 부족하므로, 의미론은 따르되 QStar manifest와 generated registry를
사용한다.

## 결과

- Active public API와 build graph는 wide compile break를 허용한다.
- 전환하지 않은 target은 alias 없이 제거한다.
- Protocol-free library embed test와 둘 이상의 protocol product가 OS 독립성 gate가 된다.
- BIOS·UEFI firmware 개발 기반과 완전한 firmware 구현 evidence를 구분한다.
- 기존 ADR 본문은 historical record로 남고 이 ADR이 active architecture authority가 된다.
