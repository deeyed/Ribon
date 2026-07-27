---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-07-27
code_paths:
  - include/Ribon/boot/plan.h
  - include/Ribon/boot/transfer.h
  - include/Ribon/service/directory.h
  - src/common/boot.c
  - src/environments/host/
  - src/environments/raw-fdt/
  - src/environments/uefi-app/
  - products/bootmgr/
  - targets/x86_64-uefi-app/
tests:
  - make check-boot-lifecycle
  - make check
  - make check-target-builds
  - make qemu-aarch64-virt-raw-fdt-smoke
  - make x86_64-uefi-app-smoke
  - qstar --file qstar.lua check
  - make docs
hardware:
  - not-run
supersedes:
  - RibonBootSession lifecycle implementation
---

# R7 Bounded Boot Lifecycle hard cut 기록

## 구현 범위

- `RibonBootTransaction`과 exact ten-stage transaction ABI 추가
- legacy session/request/prepare/commit/quiesce/transfer symbol과 path 제거
- pointer-free failure receipt, source retry counter, monotonic deadline gate 추가
- persistent metadata write와 storage flush로 `COMMIT_ATTEMPT` durable boundary 구현
- typed `environment-quiesce` service와 host/raw-FDT/UEFI provider 추가
- raw-FDT target과 x86_64 UEFI target을 transaction API로 이관
- UEFI final memory-map refresh가 source 재선택 없이 handoff만 갱신하도록 연결
- lifecycle host test에 success, retry, retry exhaustion, timeout, partial metadata write,
  flush suppression, quiesce failure를 추가
- SDK ABI 3, schema, template, install manifest와 symbol allowlist hard cut

## 검증 결과

Focused lifecycle test는 `RIBON-R7-BOUNDED-LIFECYCLE-OK` marker를 기록한다. Aggregate,
target build, QEMU smoke, QStar, Sphinx/Doxygen gate의 결과는 이 기록과 같은 commit에서
검증한다.

## 증거 경계

- lifecycle failure/receipt: host unit 및 integration fixture
- BIOS client: compile-only
- RPi5: package-only, physical UART 실행 없음
- AArch64 raw-FDT와 x86_64 UEFI: QEMU smoke가 별도 필요
- persistent storage의 실제 power-loss durability, network OTA, signature crypto,
  hardware firmware conformance: 이 기록으로 주장하지 않음
