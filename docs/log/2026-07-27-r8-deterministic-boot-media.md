---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-07-27
code_paths:
  - include/Ribon/storage/
  - include/Ribon/filesystem/
  - include/Ribon/config/
  - src/storage/
  - src/filesystems/
  - src/config/
  - src/environments/uefi-app/
  - targets/x86_64-uefi-app/
tests:
  - make check-media-pipeline
  - make check
  - make check-target-builds
  - make qemu-aarch64-virt-raw-fdt-smoke
  - make x86_64-uefi-app-smoke
  - qstar --file qstar.lua check
  - make docs
hardware:
  - not-run
supersedes:
  - embedded UEFI payload smoke evidence
---

# R8 결정론적 boot media pipeline 기록

## 구현 범위

- generic read-only block descriptor와 GPT/protective MBR byte parser 추가
- read-only FAT32 BPB, 8.3 directory, regular-file, cluster-chain reader 추가
- bounded config candidate grammar, priority selection, explicit protocol/image/kernel/module/cmdline
  representation 추가
- UEFI loaded-image device의 Simple File System file source와 optional Block I/O adapter 추가
- x86_64 UEFI ESP에 `RIBON/BOOT.CFG`와 `RIBON/PAYLOAD.ELF`를 배치하고 embedded payload runtime
  object 제거
- normal product의 writer/network authority와 UEFI embedded object graph 부재 lint 추가
- malformed GPT/FAT/config corpus와 deterministic mutation replay 추가

## 검증 결과

Focused media corpus는 `RIBON-R8-MEDIA-FUZZ-REPLAY-OK` marker를 기록했다. x86_64 UEFI QEMU
smoke는 `RIBON-R8-UEFI-CONFIG-OK`, `RIBON-R8-UEFI-ESP-PAYLOAD-OK`와 fixture entry marker를
기록했다. Aggregate, target build, raw-FDT regression, QStar와 documentation gate도 이
commit에서 모두 통과했다.

## 증거 경계

- GPT/FAT/config malformed input과 mutation: host unit/fixture-replay
- x86_64 UEFI ESP file source: QEMU smoke
- AArch64 raw-FDT memory source: 별도 QEMU smoke regression
- RPi5: package-only, live UART 없음
- BIOS: compile-only
- UEFI native Block I/O controller, physical disk media, writable update, network source,
  signature/anti-rollback, actual Parus kernel: 이 기록으로 주장하지 않음
