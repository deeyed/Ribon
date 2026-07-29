---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-07-29
code_paths:
  - tests/fixtures/riscv64/
  - products/bootmgr/manifests/qemu-riscv64-virt-rph1-fixture.json
  - tools/qemu_target_smoke.py
  - targets/qemu-riscv64-virt-opensbi/
tests:
  - make qemu-riscv64-virt-rph1-fixture-smoke
  - make check-qemu-evidence
  - make check
  - make qstar-check
  - make docs
hardware:
  - not-run
supersedes:
  - none
---

# W3 RISC-V RPH1 독립 fixture 기록

## 구현 범위

- Ribon tree가 소유하는 RISC-V 64 ELF contract consumer를 추가했다.
- External Parus kernel product와 다른 fixture manifest, build directory, provenance와
  marker graph를 사용했다.
- Consumer는 entry flag, `satp`, `sstatus.SIE`, RPH1 header/CRC32C, RISC-V provenance와
  `BOOT_CPU` singleton을 독립 검증한다.
- Harness가 RISC-V fixture success/failure marker를 payload class와 terminal outcome에
  반영하도록 확장했다.
- QStar target graph에는 fixture product build만 등록하고 QEMU 실행은 명시적 smoke
  target에 남겼다.

## 검증 결과

QEMU 11.0.2와 OpenSBI 1.7 실행에서 OpenSBI boot hart 0, Ribon raw-FDT lifecycle,
RPH1 transfer와 fixture acceptance marker가 순서대로 한 번씩 관측됐다. Harness
result는 payload와 composed image, firmware, manifest와 raw serial SHA-256,
bounded timeout, process-group cleanup을 기록했고 outcome은 `passed`였다.

관측한 fixture marker는 다음과 같다.

1. `RIBON-RPH1-RISCV64-FIXTURE-ENTRY`
2. `RIBON-RPH1-RISCV64-FIXTURE-MMU-OFF`
3. `RIBON-RPH1-RISCV64-FIXTURE-RPH1-OK`
4. `RIBON-RPH1-RISCV64-FIXTURE-BOOT-CPU-OK`
5. `RIBON-RPH1-RISCV64-FIXTURE-OK`

## 증거 경계

이 기록의 evidence class는 `Ribon RISC-V OpenSBI RPH1 contract fixture qemu-smoke`다.
실제 Parus RISC-V consumer, EB3/Sv39, SBI HSM secondary hart, SMP, RISC-V UEFI,
물리 보드, production security 또는 Parus runtime boot를 증명하지 않는다.
