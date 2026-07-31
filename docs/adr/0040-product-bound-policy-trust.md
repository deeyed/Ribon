---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/artifact/include/ribos/artifact/format.h
  - language/ribos/artifact/src/codec.c
  - tools/inspect_ribos_trust_message.py
tests:
  - make check-ribos-artifact
  - ribon-trust-message-vector-v1
hardware:
  - none
supersedes:
  - ADR 0021 canonical 112-byte signing-message decision
---

# ADR 0040: Signed policy를 product와 rollback state에 결속한다

## Context

ADR 0021의 112-byte message는 envelope version, signature algorithm, payload length와 digest,
key-ID digest만 봉인했다. 같은 payload와 key ID가 둘 이상의 product graph에서 유효하면
signature byte도 재사용할 수 있었다. Schema는 payload digest에 간접 포함됐지만 verifier가
소비하는 exact schema identity, selected mode, key usage, rollback domain과 sequence를 trust
decision의 독립 입력으로 검토하기 어려웠다.

Key ID 하나를 normal, recovery, provisioning, diagnostic, update와 image에 함께 사용하면 한
surface의 signature authority가 다른 surface로 확장될 수도 있다. Signature verification 뒤에
rollback sequence를 별도 입력으로 붙이는 방식은 message와 state 사이 substitution을 방지하지
못한다.

## Decision

- 112-byte signing-message API를 제거하고 232-byte product-bound trust-message v1으로 hard
  cut한다. Compatibility wrapper와 dual verification을 두지 않는다.
- Message는 artifact, product, schema, ABI/ISA, mode, single key usage, rollback-domain digest와
  sequence를 explicit little-endian으로 봉인한다.
- Product digest는 selected source product manifest exact bytes의 SHA-256이다. JSON
  reserialization 결과를 identity로 사용하지 않는다.
- Mode는 normal, recovery, provisioning, diagnostic의 stable registry를 사용한다.
- Key usage는 네 policy usage, update manifest와 boot image를 서로 다른 value로 둔다. Ribos
  artifact에는 mode와 policy usage가 정확히 대응해야 한다.
- Rollback state는 domain별 confirmed floor `N`과 optional pending `N+1`을 가진다. Trial은 두
  sequence만 허용하고 confirmation 뒤 `N`을 normal authority에서 거부한다.
- Product trust store는 최대 32 record, 최대 두 delegation edge를 사용한다. Network chain
  discovery와 unbounded certificate parsing을 허용하지 않는다.
- Canonical C codec와 독립 Python inspector가 tracked vector의 exact 232 byte와 SHA-256을
  공유한다.
- Authorization은 identity, key policy, signature, protected rollback state, bytecode verifier
  순으로 진행하며 첫 stable failure class를 보존한다.

## Consequences

Signature가 유효해도 다른 product, schema, mode, usage, domain 또는 sequence에서 재사용할 수
없다. Signer와 loader가 서로 다른 product manifest bytes를 사용하면 verification이
결정적으로 실패한다. Manifest formatting 변경도 새 signature를 요구하므로 release tooling은
selected source manifest를 immutable input으로 보존해야 한다.

Trust message는 Ed25519와 protected-state 구현보다 먼저 고정된다. Crypto provider, key-policy
engine과 rollback journal은 같은 bytes와 state transition을 소비해야 하며 자체 message 변형을
만들 수 없다.

Canonical vector 통과는 cryptographic correctness, key custody, revocation engine, secure storage,
power-loss recovery 또는 physical hardware를 증명하지 않는다.

## 기각한 대안

### Payload digest만 계속 서명

Product와 rollback context 사이 replay를 막지 못하므로 기각한다.

### Product와 sequence를 signature 밖 metadata로 검사

Signature가 metadata substitution을 인증하지 않으므로 기각한다.

### Key ID마다 usage를 암묵적으로 정함

잘못 구성된 trust store에서 cross-usage replay를 message 자체가 막지 못하므로 explicit single
usage를 봉인한다.

### X.509 또는 unbounded certificate chain

Pre-OS parser surface, allocation과 worst-case verification 비용을 확대하므로 bounded generated
trust table을 사용한다.

### Raw product/domain string을 variable-length message에 포함

Unicode normalization, length parsing과 canonicalization surface를 늘리므로 fixed SHA-256
identity를 사용한다.
