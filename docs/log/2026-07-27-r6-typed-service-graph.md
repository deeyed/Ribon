---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-07-27
code_paths:
  - include/Ribon/service/directory.h
  - src/core/service_directory.c
  - src/core/registry.c
  - src/environments/
  - tools/generate_plugin_registry.py
  - products/
  - qstar/manifests/
tests:
  - make check-core-service
  - make check-plugin-descriptors
  - make check-library-embed
  - make check-external-plugin
  - make check-target-builds
  - make qemu-aarch64-virt-raw-fdt-smoke
  - make x86_64-uefi-app-smoke
hardware:
  - not-run
supersedes:
  - monolithic service table implementation
---

# R6 Typed Service Graph hard cut 기록

## 구현 범위

- `RibonServiceTable` public ABI, source path, initializer와 compatibility API 삭제
- `RibonServiceDescriptor`, `RibonServiceDirectory`, authority/collection/lifetime ABI 추가
- Core ABI 3, Plugin ABI major 3, SDK ABI 2 hard cut
- product manifest의 `services`, `service_selections`, `plugin_selections`와 generated directory
- authority duplicate, missing capability, ambiguous collection, phase inversion negative gate
- host, raw-FDT, UEFI application, BIOS client의 boot-source/timer provider migration
- protocol-free embed와 out-of-tree external diagnostic package의 typed service coverage

## 검증 결과

Focused Core directory, plugin graph, protocol-free library embed, external package contract를
통과했다. Public API layout, composition schema, monolithic-service hard-cut gate도 통과했다.

`make check-target-builds`는 BIOS consumer compile-only, RPi5 package-only, AArch64 raw-FDT
image와 x86_64 UEFI application을 생성하고 target object graph를 통과했다.

`make qemu-aarch64-virt-raw-fdt-smoke`는
`RIBON-R4-QEMU-SMOKE-OK aarch64-virt-raw-fdt`를, `make x86_64-uefi-app-smoke`는
`RIBON-R4-QEMU-SMOKE-OK x86_64-uefi`를 기록했다. 두 marker의 R4는 consumer target 계약
세대이며 typed service graph의 별도 ABI version을 뜻하지 않는다.

## 증거 경계

- typed graph, SDK package, host product: `unit` 또는 host integration
- BIOS client: `compile-only`
- RPi5: `package-only`, physical UART 실행 없음
- AArch64 raw-FDT와 x86_64 UEFI: `qemu-smoke`
- physical hardware와 firmware conformance: 실행 없음
