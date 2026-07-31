---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - include/Ribon/security/signature.h
  - include/Ribon/security/ed25519.h
  - src/security/signature.c
  - src/plugins/security/ed25519/provider.c
  - third_party/monocypher/4.0.3/
  - tools/sign_ribos_policy.py
tests:
  - make check-security-ed25519-provider
  - make check-security-ed25519-cross-compile
  - make check-security-provider-graphs
hardware:
  - none
supersedes:
  - ADR 0036 R18 synthetic signature fixture decision
---

# ADR 0041: Monocypher 기반 freestanding Ed25519 provider를 선택한다

## Context

Product-bound 232-byte trust message는 서명 입력을 고정했지만 cryptographic equation을 실행하지
않았다. R18은 반복 가능한 합성 signature byte를 확인했으므로 object mutation이나 다른 private
key가 만든 signature를 구분할 수 없었다. Pre-OS target에는 libc, heap과 OS entropy 없이 동작하는
verification closure가 필요하고, target signer와 private key는 공격 표면에서 제외해야 한다.

Upstream implementation을 임의로 수정하면 provenance와 cryptographic review 범위가 흐려진다.
반대로 Monocypher의 호환성 지향 Ed25519 verifier를 그대로 노출하면 Ribon이 요구하는 canonical
point와 low-order rejection profile을 고정하지 못한다.

## Decision

- Generic verification-only provider ABI를 `Ribon/security/signature.h`에 둔다. ABI는 heap과
  provider-owned persistent state를 허용하지 않고 optional caller-owned workspace를 명시한다.
- Ed25519 equation은 official Monocypher 4.0.3 release의 byte-for-byte vendored source를 사용한다.
- Ribon wrapper가 public key와 signature `R`의 canonical encoding과 low-order point를 먼저
  거부한 뒤 `crypto_ed25519_check`를 호출한다. Strict predicate는 libsodium ref10에서
  출처와 license를 보존해 적용한다.
- Offline host signer는 OpenSSL Ed25519를 사용한다. Target ABI, source closure와 final image에는
  signer 또는 private key를 포함하지 않는다.
- Product manifest가 production/fixture class와 provider symbol을 명시하고 generated registry가
  exact source-manifest digest와 함께 선택한다.
- Final link는 function/data section garbage collection을 사용하며 security graph lint가 세 target
  image에서 signer symbol과 test seed의 부재를 검사한다.
- RFC 8032, independent OpenSSL signature, strict hostile input과 모든 single-byte mutation을
  aggregate gate로 유지한다.

## Consequences

R18 target은 synthetic byte marker 대신 canonical product-bound message의 실제 Ed25519 signature를
검증한다. Generic Core와 Ribos VM은 Monocypher, OpenSSL 또는 key source를 알지 않고 product가
선택한 provider interface만 소비한다. 다른 signature algorithm은 새 provider와 manifest/schema
version을 통해 추가하며 Ed25519 callback 내부 option으로 넣지 않는다.

Upstream translation unit에는 signing entry가 존재하지만 linked production closure에서는 제거된다.
이를 source absence라고 주장하지 않고 final target map/image gate로 검증한다. Monocypher의 검토와
timing claim도 Ribon 전체의 cryptographic certification으로 확대하지 않는다.

Key ID가 어떤 public key를 승인하는지, mode/usage/revocation과 sequence를 어떻게 판단하는지는
별도 key-policy 단계가 소유한다. Protected rollback state와 physical secure boot도 이 결정의
증거가 아니다.

## 기각한 대안

### 자체 curve와 field arithmetic 구현

검토 범위와 구현 위험을 크게 늘리고 기존 cryptographic evidence를 재사용할 수 없어 기각한다.

### Target에 OpenSSL 또는 host signer 포함

Freestanding closure를 깨고 private-key surface를 firmware에 도입하므로 기각한다.

### Upstream permissive point encoding을 그대로 승인

Ribon artifact identity에 둘 이상의 encoding을 허용하므로 strict wrapper를 둔다.

### Fixture callback을 production graph와 함께 링크

Selection 오류가 cryptographic bypass로 이어질 수 있으므로 class를 product graph에서 hard cut한다.
