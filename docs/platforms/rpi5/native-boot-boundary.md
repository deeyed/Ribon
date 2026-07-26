---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-26
code_paths:
  - src/arch/aarch64/
  - src/environments/raw-fdt/
  - src/common/drivers/
  - platforms/raspberrypi/rpi5/
  - targets/rpi5-aarch64-raw-fdt/
tests:
  - ribon-rpi5-package
  - ribon-rpi5-object-graph-lint
  - ribon-rpi5-live-uart
hardware:
  - rpi5
supersedes:
  - RPi frontend boundary
  - docs/RPI5_NATIVE.md
---

# RPi5 native target 경계

RPi5는 Ribon architecture나 firmware kind가 아니라 다음 target 조합이다.

```text
boot-manager product
  + AArch64 backend
  + raw-FDT 또는 명시적인 VideoCore environment
  + BCM2712/RPi5 platform package
  + selected Boot Protocol
  + Raspberry Pi image recipe
```

QEMU `virt`는 이 target의 board variant가 아니다.

## Entry 소유권

| 책임 | 소유자 |
| --- | --- |
| BSS, stack, CPU register normalization | AArch64 early backend |
| firmware register와 FDT capture | raw-FDT 또는 VideoCore environment |
| BCM2712/RPi5 resource와 fallback | RPi5 platform package |
| kernel8 image header와 load constraint | RPi5 image recipe |
| OS handoff와 register ABI | selected Boot Protocol |
| final cache sync와 transfer | AArch64 backend |

Entry assembly는 native state를 보존하고 environment capture로 전달하는 것 이상을
수행하지 않는다. OS wire builder, filesystem, network, update policy를 호출하지 않는다.

## Firmware 입력

Environment는 firmware register, FDT pointer, exception level, MMU/cache state를 보존한다.
FDT의 `/memory`, `/reserved-memory`, `/chosen`, UART 후보를 bounded parser로 검증하고
platform facts로 승격한다.

RAM 크기, UART base, reserved range를 generic Core 상수로 고정하지 않는다. Fallback이
필요하면 RPi5 platform descriptor가 제공하고 runtime-discovered facts와 충돌할 때
fail-closed한다.

## 공통 component

FDT parser와 PL011 driver는 RPi5 package에 복제하지 않는다.

```text
src/common/drivers/serial/pl011
src/common/sys/fdt
```

RPi5 package는 resource, compatible, wiring, firmware quirk만 제공한다.

## Package와 image recipe

Target image recipe는 다음을 소유한다.

- firmware가 로드하는 Ribon image header
- linker layout과 load window
- selected boot bundle 또는 boot source
- `config.txt`와 `cmdline.txt`
- padding, digest, signing, package manifest

Package 생성은 live boot evidence가 아니다. Package gate는 file, size, digest, target
identity, image header, selected object manifest만 검증한다.

## Boot Protocol

RPi5 target은 Parus, Linux 또는 다른 protocol을 선택할 수 있다. RPi5 platform package는
RPH1이나 특정 OS command line 의미론을 알지 않는다.

Permanent higher-half와 runtime page table은 booted OS가 소유한다.

## QEMU 분리

QEMU `virt` target은 다음을 공유할 수 있다.

- AArch64 backend
- raw-FDT environment
- FDT parser
- PL011 driver

다음은 공유하지 않는다.

- board ID와 firmware identity
- RAM fallback과 reserved resource
- linker와 image recipe
- `config.txt`
- RPi5 artifact name
- hardware evidence marker

## 실기기 evidence

RPi5 지원 주장은 fresh UART capture를 요구한다. Capture에는 source revision, image
digest, product descriptor digest, board revision, EEPROM/firmware, power, storage, UART
wiring, complete raw log를 연결한다.

QEMU `virt`, package, preserved fixture는 RPi5 hardware evidence를 대신하지 않는다.

## 안전

Ribon은 RP1 GPIO/PWM 또는 motor output을 활성화하지 않는다. Actuator inhibit,
hardware watchdog, external safety controller는 별도 product와 safety contract를 따른다.
