---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-26
code_paths:
  - src/firmware/
  - products/firmware/
  - include/Ribon/firmware/personality.h
tests:
  - ribon-firmware-personality-publication
  - ribon-firmware-consumer-provider-object-graph
hardware:
  - none
supersedes:
  - none
---

# Firmware Provider Reference Product 계약

Firmware provider reference product는 Firmware Personality ABI와 product composer의
최소 실행 가능한 계약 fixture다. UEFI application과 BIOS client 같은 consumer
product와 target ID, artifact, object graph를 공유하지 않는다.

## UEFI-compatible reference

UEFI-compatible reference는 caller-owned bounded handle/protocol database를 personality
안에 publish한다. 중복 handle/protocol tuple과 capacity 초과를 거부한다.

Variable, time, reset, image, event, console, block, filesystem, network service는 선택된
descriptor가 없으면 `UNSUPPORTED`로 실패한다. Minimal handle database publication은
UEFI specification conformance 또는 bootable UEFI firmware를 뜻하지 않는다.

## BIOS-compatible reference

BIOS-compatible reference는 caller-owned bounded E820 table을 personality 안에 publish한다.
0-length, overflow, overlap과 capacity 초과 range를 거부한다.

EDD, video, ACPI, SMBIOS, reset vector와 option ROM policy는 선택된 descriptor가 없으면
`UNSUPPORTED`로 실패한다. Minimal E820 publication은 legacy BIOS conformance 또는
bootable BIOS image를 뜻하지 않는다.

## Service directory

Service directory storage와 provider context는 caller가 소유한다. Generic Core registry가
아니며 selected personality lifetime을 벗어나지 않는다.

Publication은 descriptor ABI, stable ID order, unique service bit, phase, lifetime,
operation table과 runtime subset을 먼저 검증한다. 실패 시 directory count와 published
mask는 0으로 남는다.

`RUNTIME` service는 firmware product descriptor에서만 허용한다. Library와 bootloader
product graph는 runtime-phase plugin을 fail-closed로 거부한다.
