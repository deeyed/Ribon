---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-08-01
code_paths:
  - include/Ribon/update/storage.h
  - src/update/storage.c
  - tools/update_layout.py
  - products/validation/manifests/update-storage-reference.json
  - tests/update/
tests:
  - make check-update-storage
  - make check-update-storage-sanitizer
  - make check-update-storage-cross-compile
  - make check-update-storage-graphs
hardware:
  - not-run
supersedes:
  - none
---

# D02 bounded update storage 실행 기록

## 구현 결과

- Allocation-free generic provider ABI가 exact read/write, erase, flush와 media geometry를 고정했다.
- Source-neutral calculator가 bootloader, immutable recovery, equal A/B slots, metadata, journal과 guard를
  포함한 11-region layout을 만들고 exact 512-byte identity를 생성한다.
- Slot metadata는 exact 512-byte LE wire, body SHA-256, CRC32C와 independent reader를 사용한다.
- Metadata state machine은 active confirmed slot을 보호하고 inactive slot만
  `STAGING -> VERIFIED -> PENDING -> CONFIRMED`로 전이한다.
- Semantic slot handle은 media/layout digest와 metadata generation을 결속하며 stale handle, short I/O,
  alignment, capacity와 arithmetic error를 fail-closed 처리한다.
- Test-only memory/file provider와 hostile corpus를 추가했다.
- Product composer는 recovery/provisioning `update_storage` binding 없이는 inactive writer service와
  capability를 거부한다. Normal product source graph 14개를 writer reachability에 대해 검사했다.

## Host evidence

C와 Python layout encoder는 canonical fixture에서 같은 512 bytes와
`1e1a7f165fa068dc048189919b6e4de7d28a31710b82451d0bcbb86c8396342a` identity digest를 만들었다.
Python inspector는 C metadata wire를 독립적으로 열고 corruption과 truncation을 거부했다. Unit과
sanitizer corpus는 layout overflow/capacity, D01 manifest maximum range, metadata reserved/digest/state,
active protection, stale handle, short read/write, provider I/O failure를 실행했다.

## 증명하지 않는 것

RPi5 실기기는 실행하지 않았다. UEFI Block I/O와 board flash driver, network download, full update
transaction, redundant metadata media commit, physical power-loss recovery, wear behavior와 production
secure boot는 이 기록의 evidence가 아니다.
