---
doc_type: roadmap
status: accepted
authority: informative
last_verified: 2026-07-26
code_paths:
  - docs/
  - include/Ribon/
  - src/
  - tests/
  - tools/
tests:
  - ribon-documentation-quality-lint
hardware:
  - none
supersedes:
  - round-based Ribon roadmaps
---

# Ribon 개발 프로그램

이 roadmap은 의존 순서를 설명하며 구현 또는 검증 성공을 뜻하지 않는다.

## R0 설계 결정 동결

- 문서 위계와 품질 gate
- legacy OS semantic hard cut
- Core·Profile·Platform 경계
- Parus Handoff v1
- kernel-owned higher-half
- OTA authority
- overseer와 watchdog
- recovery network
- BIOS와 RISC-V support tier

## R1 Parus profile 전환

- Legacy profile source와 marker 제거
- Parus profile registry
- RPH1 producer와 Parus consumer
- entry flag 및 register ABI
- malformed corpus

## R2 Core service 경계

- Platform operation table
- Architecture operation table
- fixed arena와 resource limit
- mode별 object graph
- profile capability와 confirmation API

## R3 Entry bridge

- AMD64 UEFI identity bridge
- AMD64 BIOS long-mode bridge
- AArch64 entry normalization
- RISC-V S-mode entry
- Parus permanent higher-half 인수

## R4 Verified update

- Signed boot bundle
- redundant slot metadata
- anti-rollback
- pending, confirmation, rollback
- power-loss fault injection

## R5 UEFI recovery network

- SNP 또는 firmware HTTP adapter
- bounded address configuration과 download
- streaming write와 digest
- timeout과 malformed packet corpus

## R6 BIOS

- stage0와 stage1
- EDD, E820, ACPI
- GPT 또는 fixed slot
- Parus Handoff v1
- optional PXE

## R7 RISC-V

- QEMU virt와 OpenSBI
- FDT와 SBI extension
- Parus Handoff v1
- UEFI application

## R8 RPi5 복구와 감독

- Native storage A/B
- watchdog와 reset reason
- recovery transport
- external safety controller protocol
- live power-loss와 UART evidence

## R9 제품 hardening

- parser fuzzing
- cryptographic and key-rotation validation
- reproducible build와 SBOM
- signed release
- board별 evidence matrix
