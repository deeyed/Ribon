---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-08-14
code_paths:
  - include/Ribon/protocols/os/luca/
  - src/protocols/os/luca/
  - products/bootmgr/manifests/
tests:
  - rlh1_builder_tests
  - luca_entry_contract_tests
  - qemu-riscv64-virt-rlh1-fixture-smoke
hardware:
  - none
supersedes:
  - 0003-parus-handoff-v1 wire authority
  - RPH1 compatibility
---

# ADR: RLH1 LUCA handoff로 비호환 hard cut한다

## 맥락

Ribon은 제품 OS인 Parus와 재사용 kernel인 LUCA를 구분해야 한다. 기존
RPH1은 wire magic, protocol ID, C symbol과 문서 권위에 Parus를 결속했다.
이를 alias나 dual parser로 유지하면 boot producer와 consumer의 선택이 모호해지고
downgrade 경로가 남는다.

## 결정

1. Boot protocol ID를 `luca`, plugin ID를 `protocol.luca`로 고정한다.
2. Handoff format은 Ribon LUCA Handoff v1, `RLH1`로 고정한다.
3. Magic은 ASCII `RLH1`, header는 80 byte, domain separator는
   `RIBON_LUCA_RLH1\0`로 고정한다.
4. RPH1 magic, 64-byte header, old symbol/path/package를 전부 거부한다.
5. 호환 alias, forwarding header, dual parser, 자동 fallback을 두지 않는다.
6. Parus는 이 프로토콜을 선택하는 product identity로만 남을 수 있다.

## 안전 경계

RLH1 domain separator와 CRC32C는 type confusion과 손상을 검출하지만 signature나
rollback provider가 아니다. Production promotion은 producer evidence, selected bundle digest,
protected monotonic floor와 LUCA consumer의 provenance 검증을 별도로 필요로 한다.
QEMU fixture는 register/wire 회귀 증거이며 physical hardware 증거가 아니다.

## 반증 기준

- old RPH1 magic이 parser를 통과하면 이 결정은 실패다.
- RLH1 magic과 old 64-byte header를 혼합한 artifact가 통과하면 실패다.
- wrong domain, unknown required section, checksum corruption을 통과시키면 실패다.
- RLH1 실패 후 RPH1/direct-DTB로 fallback하면 실패다.
