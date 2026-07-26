---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-26
code_paths:
  - include/Ribon/firmware/
  - src/environments/
  - src/firmware/
  - products/firmware/
tests:
  - ribon-environment-direction-lint
  - ribon-firmware-personality-tests
  - ribon-firmware-product-smoke
hardware:
  - none
supersedes:
  - undirected firmware adapter
---

# Environment와 Firmware Personality 계약

Ribon은 firmware consumer와 firmware provider를 서로 다른 plugin kind와 dependency
방향으로 구분한다.

## Consumer

Environment Consumer는 기존 firmware가 제공하는 service를 Ribon service table로
변환한다.

| Consumer | 소비하는 ABI |
| --- | --- |
| UEFI application | UEFI System Table과 Boot Services |
| BIOS client | legacy interrupt, E820, EDD, video service |
| raw-FDT | 초기 register와 FDT |
| SBI | hart ID, FDT, SBI extension |

Consumer는 외부에 firmware-compatible service를 publish하지 않는다.

## Provider

Firmware Personality는 Ribon service와 driver를 외부 firmware ABI로 publish한다.

| Personality | 제공하는 ABI |
| --- | --- |
| UEFI-compatible | System Table, Boot Services, 선택적 Runtime Services |
| BIOS-compatible | 선택된 interrupt service와 firmware table |

Provider는 generic Core를 fork하지 않는다. Personality가 요구하는 handle database,
event, protocol publication, variable namespace는 personality runtime에 한정한다.

## Service table

Firmware service는 GUID 또는 stable service ID, ABI version, phase, ownership, operation
table을 가진다. Native ABI table로 publish하기 전에 generic provider의 capability와
lifetime을 검증한다.

Service가 `QUIESCE`에서 종료되면 이후 Boot Library와 protocol이 호출할 수 없다.
`RUNTIME` service는 personality manifest에 명시되고 OS entry 뒤 보존되는 memory와
calling convention을 별도 검증한다.

## UEFI personality 최소 분해

UEFI-compatible product는 다음 module을 독립 plugin으로 둘 수 있다.

- memory and event foundation
- handle and protocol database
- image service와 PE/COFF loader binding
- device path와 boot manager integration
- variable, time, reset service
- console, block, filesystem, network driver
- configuration table publication
- ExitBootServices transaction

UEFI conformance는 module 존재가 아니라 선택된 UEFI specification과 test suite의
독립 evidence를 요구한다.

## BIOS personality 최소 분해

BIOS-compatible product는 다음을 독립 component로 둔다.

- reset vector와 real-mode foundation
- E820 memory service
- EDD disk service
- console/video service
- ACPI와 SMBIOS publication
- protected/long-mode transition helper
- option ROM 정책

BIOS client 성공은 BIOS personality 구현 증거가 아니다. QEMU/SeaBIOS에서 실행된
Ribon loader는 기존 BIOS를 소비한 증거다.

## Plugin SDK

외부 firmware module은 `plugin.qst`, public header, source, contract test, documentation을
가진 package로 제공한다. SDK는 fake service, host harness, ABI validator, product
composer, image recipe template을 제공한다.

Dynamic module loading은 firmware personality가 명시적인 trust와 relocation 계약을
채택할 때만 허용한다. Generic bootloader product는 정적 composition을 사용한다.
