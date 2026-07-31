---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-07-31
code_paths:
  - language/ribos/artifact/include/ribos/artifact/format.h
  - language/ribos/artifact/src/codec.c
  - tools/inspect_ribos_trust_message.py
  - tests/fixtures/security/ribos-policy-trust-v1.json
tests:
  - make check-ribos-artifact
  - make check-ribos-verifier
  - make qstar-check
  - make docs
hardware:
  - not-run
supersedes:
  - none
---

# R03 production trust 계약 동결 기록

## 구현

R03는 payload length/hash와 key-ID hash만 포함하던 112-byte signature message를 제거하고,
232-byte `RIBON-TRUST-MESSAGE-V1`으로 hard cut했다. 새 C codec는 artifact, selected source
product manifest, schema, envelope/VM/ISA, mode, single key usage, rollback domain과 sequence를
explicit little-endian으로 봉인한다.

Tracked JSON vector를 독립 Python inspector와 C codec가 각각 직렬화한다. Fixed vector는 다음
identity를 가진다.

- message bytes: 232
- message SHA-256: `e71a82609bfb3a646fa1be698da94f2c791aad4c5ec5c23f31c940e2e2315444`
- mode: normal
- usage: normal Ribos policy
- sequence: `0x0102030405060708`
- payload length: `0x11223344`

Artifact codec unit은 unsupported version/algorithm, invalid mode/usage, policy usage mismatch,
zero identity, nonzero reserved와 genesis sequence를 검사한다. Existing artifact hostile corpus에는
envelope reserved와 unsupported minor mutation을 추가했다.

두 normative security contract는 최대 32개 key record, 최대 두 delegation edge, single usage,
sequence-bound rotation/revocation 및 domain별 `confirmed N`/`pending N+1` trial 의미를 고정한다.
Authorization ordering은 structural, identity, key policy, signature, protected rollback state,
Stage-1/2 verifier와 PreparedProgram 순서다.

## 검증 결과

- `RIBOS-ARTIFACT-TEST-OK ... trust-message=product-bound ... mutations=7`
- `RIBON-TRUST-MESSAGE-V1-OK bytes=232 cross-tool=2`
- `RIBOS-VERIFIER-CORPUS-OK positive=5 hostile=25 compiler-trusted=0`
- QStar graph: `target-count 39`, `generated-action-count 13`, `status ok`
- Documentation quality lint: hard-forbidden wording 0, Doxygen 누락 0
- Sphinx `-W --keep-going`, Doxygen/Breathe build: 성공
- `git diff --check`: 성공

## 증거 경계

이 결과는 `unit/contract` evidence다. Canonical bytes와 failure ordering을 검증했지만 Ed25519
cryptographic verification, production key store, delegation/revocation engine, protected rollback
journal, power-loss behavior, TPM/RPMB와 physical hardware는 실행하지 않았다. R18의 deterministic
signature byte는 계속 cryptographic proof가 아닌 diagnostic fixture다.
