---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-08-01
code_paths:
  - include/Ribon/security/key_policy.h
  - src/security/key_policy.c
  - tools/generate_plugin_registry.py
  - qstar/schemas/product.schema.json
tests:
  - make check-security-key-policy
  - make check-security-key-policy-sanitizer
  - make check-security-key-policy-graphs
  - make check-ribos-r18
hardware:
  - none
supersedes:
  - product-local hardcoded public-key selection
---

# ADR 0042: Product graph가 bounded immutable key policy를 생성한다

## Context

Canonical trust message와 freestanding Ed25519 provider는 object binding과 cryptographic
equation을 고정했지만, key ID가 어떤 public key, mode, usage, product, rollback domain과 sequence를
승인하는지는 product callback의 hardcoded 비교에 남아 있었다. 이 방식은 rotation, revocation과
delegation을 조합 단계에서 검증할 수 없고, 서로 다른 product가 암묵적으로 key authority를
공유할 위험이 있다.

Pre-OS runtime은 network certificate discovery, heap, wall clock과 recursive path building에
의존하지 않아야 한다. 동시에 generated table을 compiler output이라는 이유만으로 신뢰해서도
안 된다.

## Decision

- Source product manifest가 stable store ID, generation과 최대 32개 key record를 소유한다.
- Record는 exact key ID/public key, product digest, mode/usage, 최대 네 rollback domain,
  inclusive sequence 범위, lifecycle과 optional issuer를 명시한다.
- Delegation은 root 아래 최대 두 edge이며 child authority는 모든 축에서 issuer authority의
  부분집합이어야 한다.
- Composer는 lifecycle과 무관한 duplicate identity, unknown issuer, cycle, depth overflow와 authority expansion을
  거부하고 stable key-ID 순 immutable C table을 생성한다.
- Composer와 runtime은 pointer/C layout을 제외한 versioned little-endian serialization으로
  canonical store digest를 각각 계산한다.
- Runtime은 table 정렬, public-key identity, issuer graph와 authority containment를 다시 유도한다.
- Key policy가 성공한 뒤에만 exact selected record의 public key로 signature provider를 호출한다.
- Public API의 decision은 pointer-free이며 key/store/provider authority를 Ribos VM에 전달하지 않는다.
- Signature provider와 key policy selection은 product graph에서 함께 존재하거나 함께 없어야 한다.

## Consequences

Rotation overlap, retiring key, revoked issuer descendant와 bounded two-edge delegation을 firmware에서
allocation 없이 설명하고 검증할 수 있다. Normal Ribos product의 mode/usage authority leakage는
composition과 final object graph 양쪽에서 hard gate가 된다. Product-specific public key는 generic
Core, VM 또는 adapter source에 들어가지 않는다.

Store generation은 immutable trust-store identity이지 protected rollback floor가 아니다. 새 store의
설치, atomic activation, hardware root key, private-key custody와 rollback journal은 별도 update 및
protected-state 계약이 소유한다.

## 기각한 대안

### Product callback에 key-ID 비교 유지

회전과 delegation semantics가 product마다 달라지고 graph audit가 불가능하므로 기각한다.

### X.509 또는 network certificate path discovery

Parser와 dynamic discovery surface를 늘리고 bounded worst-case를 깨므로 v1에서 기각한다.

### Key record를 Ribos value로 전달

Policy가 trust authority를 관찰하거나 재조합할 수 있으므로 pointer-free decision만 노출한다.

### Store generation을 rollback counter로 재사용

Trust-store 교체와 signed-object trial/confirmation은 서로 다른 transaction이므로 분리한다.
