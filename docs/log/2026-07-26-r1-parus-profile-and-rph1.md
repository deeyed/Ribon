---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-07-26
code_paths:
  - stand/Ribon/include/Ribon/profiles/parus/
  - stand/Ribon/src/profiles/parus/
  - stand/Ribon/src/boot/
  - stand/Ribon/tools/lint/legacy_os_hard_cut.py
tests:
  - ribon-parus-handoff-v1-unit
  - ribon-entry-abi-fixture-amd64-qemu
  - ribon-entry-abi-fixture-aarch64-qemu
  - ribon-parus-kernel-amd64-qemu
  - ribon-parus-kernel-aarch64-qemu
hardware:
  - none
supersedes:
  - none
---

# 2026-07-26 R1 Parus profile과 RPH1 기록

## Source 기준

- branch: `parus`
- 시작 revision: `2a76d03744e092dc0bbeb9efde62f112815bd1e2`
- 변경 경계: `stand/Ribon/**`
- evidence class: host unit, cross-compile, QEMU VM

## 구현

- Builtin profile registry를 `parus` 단일 profile로 교체했다.
- `RPH1` 64-byte header, 32-byte section entry, 16-byte payload alignment,
  65,536-byte 상한, CRC32C를 byte-wise serializer로 구현했다.
- Bounded parser가 header, CRC, table bounds, section bounds와 overlap, required와
  unknown section, singleton duplication, payload shape를 검증한다.
- Producer는 생성한 artifact를 반환하기 전에 parser로 자체 검증한다.
- Byte writer는 compiler가 unaligned multi-byte store로 결합하지 못하도록 byte access를
  고정한다. AArch64 EL1 alignment-check 환경에서도 같은 wire ABI를 생성한다.
- AMD64 `rdi/rsi`, AArch64 `x0/x1`, RISC-V 64 `a0/a1`의 pointer/flag 의미를
  공통 entry contract로 고정했다.
- Normal flag는 `0x1`, direct-high flag는 `0xd`다.
- Raspberry Pi entry도 `x0=RPH1`, `x1=entry_flags` 두 register ABI로 교체했다.
- Active tree path와 content를 검사하는 `legacy-hard-cut` gate를 추가했다.

## Ribon 독립 검증

- `make check-rph1`: `RPH1-TEST-OK`
  - valid artifact, CRC corruption, unsupported version, unknown required section,
    duplicate singleton, overlapping payload, nonzero reserved field를 검증했다.
- `make RIBON_ARCH=x86_64 qemu-uefi-smoke`: `RIBON-UEFI-SMOKE-OK`
  - `RPH1-SECTIONS=8`, `JUMP-FLAGS=0x1`, ABI-aware fixture entry marker 확인.
- `make RIBON_ARCH=aarch64 qemu-uefi-smoke`: `RIBON-UEFI-SMOKE-OK`
  - `RPH1-SECTIONS=7`, `JUMP-FLAGS=0x1`, ABI-aware fixture entry marker 확인.
- `make qemu-rpi-smoke`: `RIBON-RPI-SMOKE-OK`
  - EL1 alignment-check 상태에서 RPH1 생성, parser 자체 검증, `x0/x1` handoff marker 확인.
- Ribon UEFI SHA-256:
  - AMD64 actual-Parus 실행본:
    `d322ba4b92e850db5a3e1727157925645fcf88c803700ebd45b51038ad3ecad6`
  - AArch64 actual-Parus 실행본:
    `40016cc1de55d602c1a1547d6328742b4bdc46a95a7e6315bd1cdb271ac3f7c1`

Fixture marker는 handoff pointer가 nonzero이고 entry flag가 `0x1`일 때만 출력된다.
Fixture QEMU 통과는 실제 Parus kernel consumer 통과를 뜻하지 않는다.

## 실제 Parus ELF 통합 결과

AMD64 입력 ELF:

- path: `build/target/debug/amd64/amd64-uefi/parus-amd64-uefi.elf`
- SHA-256: `80cacfdd48bdeb72b1c2c728d698059b6b577f44b632870eca9771d027185252`
- Ribon 관측: kernel load, 8-section RPH1 `0x1bb0` byte 생성, `ExitBootServices`,
  entry `0x100000`, entry flag `0x1`까지 도달
- 결과: kernel success marker 없이 timeout

AArch64 입력 ELF:

- path: `build/target/debug/arm64/qemu-virt-aarch64/parus-qemu-virt-aarch64-entry.elf`
- SHA-256: `6e8f1de5e7acdd8abbb6b096df8581343e604e430204cfcaf55707723f0044a9`
- Ribon 관측: kernel load, 7-section RPH1 `0x0b00` byte 생성, `ExitBootServices`,
  entry `0x40080000`, entry flag `0x1`까지 도달
- 결과: kernel success marker 없이 timeout

두 결과는 실제 Parus ELF를 실행한 QEMU 통합 증거지만 성공한 Parus boot 증거가 아니다.
작업 경계 밖의 kernel consumer가 RPH1을 수용하기 전에는 R1의 end-to-end acceptance를
닫지 않는다. Ribon fixture와 producer/parser gate는 이 외부 경계를 대체하지 않는다.

## Hard cut

- `python3 tools/lint/legacy_os_hard_cut.py`: 통과
- repository root가 `stand/Ribon`인 `rg -i <retired-identifier> .`: match 0
- path 검사: active match 0

Generated `build/`와 Git history는 active hard-cut gate의 대상이 아니다.
