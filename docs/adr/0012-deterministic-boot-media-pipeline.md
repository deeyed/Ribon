---
doc_type: adr
status: accepted
authority: normative
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
  - make x86_64-uefi-app-smoke
hardware:
  - none
supersedes:
  - x86_64 UEFI embedded payload source
---

# ADR: 결정론적 read-only boot media pipeline을 채택한다

## 맥락

Build-time embedded payload는 source discovery, filesystem path, configuration policy와
environment-native file lifetime을 실행하지 않는다. 따라서 UEFI application이 QEMU marker를
기록해도 ESP에서 실제 boot artifact를 선택하고 읽는 product를 증명하지 못한다.

Ribon은 GPT/FAT/config parser를 generic library로 제공해야 하지만 Core에 firmware handle이나
board policy를 넣을 수 없다. Normal boot product가 storage reader를 이유로 inactive update
writer나 recovery network 권한을 얻는 것도 허용할 수 없다.

## 결정

1. Generic boot media API는 caller-owned read-only block provider, byte-wise GPT/protective MBR
   parser, bounded FAT32 8.3 reader와 configuration parser로 분리한다.
2. GPT는 header/entry CRC32, overflow, usable-range와 partition overlap을 fail-closed로
   검증한다. Backup-header fallback과 repair는 제공하지 않는다.
3. FAT32는 read-only와 canonical root-contained 8.3 path만 지원한다. Writable FAT, LFN과
   다른 filesystem은 별도 protocol/service 결정 없이는 추가하지 않는다.
4. Configuration은 explicit protocol, image format, kernel, module, command line와 priority를
   parser result에 보존한다. Unknown required meaning과 priority tie는 fail-closed다.
5. UEFI consumer는 Simple File System과 optional Block I/O를 environment-private typed adapter로
   capture한다. x86_64 UEFI target은 ESP config가 선택한 external payload file을 읽으며 runtime
   embedded payload object를 링크하지 않는다.
6. Normal product graph에서 network transport와 inactive-slot writer를 lint로 금지한다.

## 결과

Boot Library는 여전히 selected `RibonBootSource`의 exact read와 lifecycle만 소유한다.
Configuration parser가 OS wire, UEFI native handle, page-table policy 또는 update state machine을
해석하지 않는다. UEFI file service가 FAT32 parser를 우회하는 것은 firmware가 제공한 Simple
File System 소비 경계이며 generic FAT32 unit/parser evidence를 대체하지 않는다.

## 기각한 대안

- UEFI target에 payload를 계속 embed하고 test tool만 ESP file을 검사하는 방식은 runtime source
  authority를 증명하지 못해 기각한다.
- FAT write/repair를 reader와 함께 추가하는 방식은 update authority를 normal boot graph에 섞어
  기각한다.
- GPT/FAT parse 실패를 memory source, other filesystem 또는 network source로 자동 fallback하는
  방식은 deterministic candidate selection을 깨므로 기각한다.
