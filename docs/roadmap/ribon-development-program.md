---
doc_type: roadmap
status: accepted
authority: informative
last_verified: 2026-07-31
code_paths:
  - docs/
  - include/Ribon/
  - src/
  - platforms/
  - products/
  - targets/
  - sdk/
tests:
  - ribon-documentation-quality-lint
hardware:
  - none
supersedes:
  - R0-R9 profile-centered development program
---

# Ribon 개발 프로그램

이 roadmap은 capability 의존 순서를 설명하며 implementation status, round cursor,
release evidence를 정의하지 않는다.

## Library foundation

- Core와 Boot Library의 caller-owned context
- stable plugin descriptor와 capability graph
- generated immutable registry
- protocol-free embed example
- public ABI와 object graph lint

## Multiprotocol boot manager

- Parus Boot Protocol
- EFI chainload
- Multiboot2
- Linux architecture별 protocol
- FreeBSD architecture별 protocol
- protocol과 image-format parser 분리

한 protocol의 성공으로 다른 protocol의 지원을 주장하지 않는다.

## Environment coverage

- host embed harness
- x86_64와 AArch64 UEFI application
- x86 BIOS client
- AArch64 raw-FDT
- RISC-V SBI
- environment별 quiesce와 final memory-map transaction

## Platform coverage

- QEMU PC
- QEMU AArch64 `virt`
- QEMU RISC-V `virt`
- Raspberry Pi 5
- 재사용 driver와 board resource package 분리

QEMU와 physical hardware evidence를 독립적으로 닫는다.

## Firmware SDK

- plugin, package, product, target, image metadata
- external plugin template와 host contract harness
- firmware lifecycle와 service publication
- UEFI-compatible personality
- BIOS-compatible personality
- firmware provider conformance evidence

기존 firmware 위의 Ribon application 성공은 firmware personality 구현 성공을 뜻하지
않는다.

## Resilient product

- signed manifest와 anti-rollback
- redundant slot journal
- bounded recovery network
- watchdog와 reset reason
- OS-specific overseer companion plugin
- power-loss fault injection

Normal boot product에는 recovery network와 update writer를 링크하지 않는다.

## Ribos verified policy runtime

- Pegen 정본 grammar와 reproducible host C parser generation
- 독립 Ribos AST, type checker와 formatter
- bounded collection과 acyclic call-graph 검사
- semantic helper typestate와 capability/effect 검사
- Policy IR CFG, frame/stack와 exact resource upper-bound closure
- canonical little-endian bytecode ISA와 signed artifact envelope
- allocation-free structural reader와 hostile-byte semantic verifier 분리
- bounded bytecode, static verifier와 signed policy artifact
- policy A/B와 immutable factory fallback
- fixed-arena developer parser와 capability-restricted shell
- board adaptation, boot, update와 recovery policy corpus

Host parser 성공은 firmware parser, verifier 또는 policy VM 실행 성공을 뜻하지 않는다.

## Product hardening

- parser fuzzing과 malformed corpus
- reproducible build와 selected-object manifest
- ABI compatibility report
- SBOM과 signed release
- architecture, environment, protocol, platform별 evidence matrix
- physical hardware capture provenance
