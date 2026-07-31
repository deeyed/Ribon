---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-08-01
code_paths:
  - include/Ribon/policy/ribos.h
  - src/plugins/policy/ribos/adapter.c
  - tools/generate_plugin_registry.py
  - qstar/schemas/product.schema.json
tests:
  - make check-ribos-ribon-integration
  - make check-ribos-r18
  - make check-ribos-product-graphs
hardware:
  - none
supersedes:
  - product callback as Ribos production authorization authority
---

# ADR 0044: Signed Ribos authorization은 generic adapter의 고정 pipeline이다

## Context

Ribon에는 canonical trust message, production Ed25519 provider, bounded key policy, protected rollback
journal, 독립 verifier와 sealed BootAction이 각각 구현되어 있었다. 그러나 Ribos product binding은
여전히 product별 `authorize` callback 하나를 호출했고 callback이 이 authority를 모두 실제로
결합했는지는 generic adapter가 보장하지 못했다. R18 validation product는 서명과 key policy를
검사했지만 protected state를 승인 경로에 사용하지 않았다.

## Decision

- Product manifest는 `fixture-callback` 또는 `signed-policy` authorization class를 exact하게 선택한다.
- `signed-policy`는 production signature provider, immutable key policy와 protected-state provider가
  모두 있어야 하며 exact rollback domain을 generated binding에 봉인한다.
- Generic adapter가 structural open, identity, key policy, Ed25519, protected state, verifier,
  PreparedProgram, VM과 sealed BootAction을 한 고정 순서로 결합한다.
- Production signed path에는 product authorizer callback을 두지 않는다.
- 새 `N+1` trial은 correctly signed candidate의 독립 Stage-1/2 preflight 뒤 시작한다. Trial VM 실행
  전 attempt 감소를 durable commit한다.
- Confirmation과 failed-trial 전이는 VM helper가 아니라 native `confirm`/`fail_trial` API가 소유한다.
- BootAction은 product validation, single consume, boot transaction commit, environment quiesce 순서로
  닫고 모든 실패는 pointer-free receipt로 factory recovery를 최대 한 번 알린다.

## Consequences

Host reference와 AMD64/AArch64/RISC-V validation product가 동일한 generic authorization pipeline을
사용한다. Product는 semantic helper와 BootAction 의미만 구현하고 signature 또는 rollback을
임의 callback으로 대체하지 못한다. Product graph가 security closure를 빠뜨리거나 fixture와
production authority를 섞으면 source generation 전에 거부된다.

Reference provider 기반 시험은 state machine, ordering과 A/B 전이를 검증하지만 hostile media replay,
physical power loss, TPM/RPMB, production key custody와 실제 OTA storage를 증명하지 않는다.

## 기각한 대안

### Product별 authorizer 유지

Product마다 검증 순서와 rollback 사용 여부가 달라져 보안 계약을 object graph만으로 설명할 수
없으므로 기각한다.

### Signature 성공 직후 trial state 기록

Correctly signed verifier-invalid candidate가 pending authority를 오염시킬 수 있으므로 verifier
preflight 뒤에만 trial을 연다.

### Ribos helper가 confirm 또는 floor를 수정

외부 policy가 자신의 trust authority를 바꿀 수 있으므로 native health/update authority에 남긴다.
