---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-07-26
code_paths:
  - include/Ribon/sdk/
  - src/plugins/
  - src/firmware/
  - sdk/
  - examples/
  - products/firmware/
  - qstar/schemas/
tests:
  - make-check
  - make-docs
  - qstar-check
  - ribon-r4-qemu-regression
hardware:
  - not-run
supersedes:
  - none
---

# R5 SDK·Product·Firmware composition 기록

## 구현 범위

- `libribon-sdk`와 deterministic install tree
- SDK/Core/Plugin/Firmware Personality ABI tuple과 global symbol allowlist
- product, target, image, plugin-package metadata schema
- source-private include가 없는 out-of-tree library embed
- manifest, public header, source, test, docs, `plugin.qst`를 가진 external service package
- personality-private caller-owned service directory
- UEFI-compatible bounded handle database reference provider
- BIOS-compatible bounded E820 reference provider
- consumer/provider artifact와 object graph 분리

## 검증 결과

`make check`는 host unit, 세 architecture host product, external SDK consumer, generated
registry, provider publication, unsupported service negative case, install reproducibility와
object graph를 통과했다.

QStar graph는 library, SDK, boot target, external package와 firmware provider product를
서로 다른 node로 검증했다.

`make check-target-builds`는 BIOS consumer compile-only artifact, RPi5 package-only
artifact, AArch64 QEMU image와 x86_64 UEFI application을 생성하고 target object graph를
통과했다.

`make qemu-aarch64-virt-raw-fdt-smoke`와 `make x86_64-uefi-app-smoke`는 각각
`RIBON-R4-QEMU-SMOKE-OK aarch64-virt-raw-fdt`와
`RIBON-R4-QEMU-SMOKE-OK x86_64-uefi`를 기록했다. Marker 이름의 R4는 consumer target
계약 세대를 나타내며 R5 provider conformance를 뜻하지 않는다.

## 증거 경계

- SDK install, external package, service publication: `unit`
- UEFI/BIOS provider artifact: `compile-only reference`
- AArch64 raw-FDT와 x86_64 UEFI consumer target: `qemu-smoke`
- RPi5: `package-only`, 실기기 실행 없음
- UEFI/BIOS conformance suite: 실행 없음

Provider reference 성공은 bootable UEFI/BIOS firmware 또는 specification conformance
주장이 아니다.
