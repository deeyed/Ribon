---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-08-01
code_paths:
  - include/Ribon/security/protected_state.h
  - src/security/protected_state.c
  - src/security/sha256.c
  - products/validation/ribos-qemu/product.c
  - tools/lint/protected_state_graph_lint.py
tests:
  - make check-security-protected-state
  - make check-security-protected-state-sanitizer
  - make check-security-protected-state-graphs
hardware:
  - not-run
supersedes:
  - none
---

# R06 protected rollback state 구현 기록

## 구현

R06은 monotonic timer와 분리된 generic protected-state provider ABI, explicit little-endian journal
codec와 confirmed/trial state machine을 추가했다. Journal은 record 두 slot과 selector 두 slot을
사용한다. 새 record write·flush·readback 뒤에만 새 selector를 쓰며, selector가 가리키는 exact
record SHA-256과 두 wire object의 CRC32C를 검사한다.

State API는 initialize, exact-successor trial 시작, pure sequence authorization, transfer 전 attempt
소비, pending confirmation과 trial failure를 제공한다. Generation/sequence wrap, provider unavailable,
domain mismatch, readback mismatch, corruption과 same-generation conflict는 fail closed한다.

Product composer는 signature provider, immutable key policy와 protected-state provider를 완전한
triple로 요구한다. Key-policy domain 합집합과 protected provider domain 집합이 다르면 target
registry 생성을 거부한다. R18 cross-architecture diagnostic product는 실제 provider graph closure를
검증하기 위해 in-memory reference provider를 선택한다.

## 검증 결과

- Host unit: confirmed → trial → attempt consume → confirm/fail transition과 rollback/skip/exhaustion
- Fault injection: record/selector flush 각각의 0..128 byte prefix, 총 258개 절단점 뒤 reboot/open
- Negative codec: selected record corruption, stale selector ordering, conflicting valid generation,
  generation wrap와 provider unavailable
- Domain isolation: normal floor 100과 recovery floor 2를 독립 namespace로 전이
- Sanitizer: AddressSanitizer와 UndefinedBehaviorSanitizer
- Product graph: signed product의 provider 누락, domain mismatch, fixture-class 혼합과 unknown class 거부
- Target closure: AMD64, AArch64, RISC-V64 generated binding과 reference provider final-map 확인

## 증거 경계

이 기록의 durability 증거는 deterministic in-memory fault simulation이다. Physical power cut, torn
flash sector, storage controller cache, TPM NV, RPMB, secure element, hostile whole-media replay와 key
provisioning은 실행하지 않았다. R18 reference provider는 cross-architecture object-graph fixture이며
production protected storage가 아니다. R06은 state engine과 provider boundary를 구현했지만 실제
update pipeline 또는 OS health confirmation을 연결하지 않았다.
