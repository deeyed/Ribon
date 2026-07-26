---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-26
code_paths:
  - src/boot/rpi_entry.S
  - src/boot/rpi_main.c
  - src/firmware/rpi/
  - linker/rpi5-aarch64.ld
  - configs/rpi5/
tests:
  - ribon-rpi5-package
  - ribon-rpi5-live-uart
hardware:
  - rpi5
supersedes:
  - docs/RPI5_NATIVE.md
---

# RPi5 native 부트 경계

RPi5 native frontend는 Raspberry Pi firmware가 로드하는 AArch64 image와 Parus boot
bundle을 연결한다. RPi5 platform detail은 Core와 Parus profile로 유출하지 않는다.

## Firmware 입력

Frontend는 firmware register, DTB pointer, entry exception level, MMU/cache state를
보존한다. DTB의 `/memory`, `/reserved-memory`, `/chosen`, UART 후보를 bounded parser로
검증한 뒤 platform facts로 승격한다.

RAM 크기, UART base, reserved range를 generic Core 상수로 고정하지 않는다. Board와
firmware가 제공한 값은 alignment, overflow, overlap 검증을 통과해야 한다.

## Package

Package는 다음 component를 명시적으로 묶는다.

- firmware가 로드하는 Ribon image
- signed Parus boot bundle 또는 staging용 kernel component
- `config.txt`
- `cmdline.txt`
- package manifest와 digest

Package 생성은 live boot evidence가 아니다. Package lint는 file presence, size, digest,
target identity만 검증한다.

## Entry

Ribon은 cache maintenance와 exception-level normalization을 수행하고 Parus Handoff v1의
AArch64 register ABI로 이전한다. Permanent higher-half는 Parus가 소유한다.

## 실기기 evidence

RPi5 지원 주장은 fresh UART capture를 요구한다. Capture에는 source revision, image digest,
board revision, EEPROM/firmware, power, storage, UART wiring, complete raw log를 연결한다.
QEMU `virt`와 preserved fixture는 RPi5 hardware evidence를 대신하지 않는다.

## 안전

Ribon은 RP1 GPIO/PWM 또는 motor output을 활성화하지 않는다. Actuator inhibit와 external
watchdog은 별도 platform safety contract를 따른다.
