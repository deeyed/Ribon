---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-08-01
code_paths:
  - language/ribos/artifact/include/ribos/artifact/format.h
  - language/ribos/artifact/src/codec.c
  - include/Ribon/security/signature.h
  - include/Ribon/security/key_policy.h
  - src/security/key_policy.c
  - src/plugins/security/ed25519/provider.c
  - tools/inspect_ribos_trust_message.py
tests:
  - make check-ribos-artifact
  - make check-security-ed25519-provider
  - make check-security-key-policy
  - make check-security-key-policy-graphs
  - ribon-trust-message-vector-v1
hardware:
  - none
supersedes:
  - Ribos 112-byte artifact-only signing message
---

# Product-bound signed object trust v1 계약

## 목적과 신뢰 경계

Ribon의 signature는 byte 무결성만 승인하지 않는다. 한 signature decision은 exact object와
product graph, schema, 실행 mode, key usage, rollback domain 및 monotonic sequence를 하나의
canonical message에 결속한다.

```text
object byte identity
  + product identity
  + schema identity
  + ABI와 ISA
  + execution mode
  + one key usage
  + rollback domain
  + sequence
  -> canonical trust message
  -> selected signature provider
  -> product key policy
  -> rollback authorization
```

Canonical message codec는 signature 연산, public-key lookup, delegation, revocation 또는
protected state I/O를 수행하지 않는다. Codec 성공은 cryptographic authorization이 아니다.

## Identity 정의

모든 identity는 SHA-256 32 byte이며 all-zero 값은 유효하지 않다.

| Identity | Canonical input |
| --- | --- |
| artifact digest | `.rba` executable payload exact bytes |
| product digest | selected source product manifest exact bytes |
| schema digest | artifact payload가 봉인한 canonical product schema |
| rollback-domain digest | product trust graph의 stable rollback-domain ID UTF-8 bytes |
| key-ID digest | envelope key ID exact bytes |

Product manifest의 공백, field order 또는 newline이 달라지면 product digest도 달라진다. Signer는
composer가 선택한 exact source manifest를 사용하며 parsed JSON을 임의로 다시 출력해 digest를
만들지 않는다. Canonical product descriptor 형식이 도입되면 새 trust-message version을
사용한다.

Key ID는 public key가 아니다. Immutable product trust store에서 한 key record를 선택하는
1..64 byte opaque selector다. Message에는 raw key ID 대신 `SHA-256(key ID)`를 기록한다.

## Stable registry

### Execution mode

| Value | Mode |
| ---: | --- |
| 1 | normal |
| 2 | recovery |
| 3 | provisioning |
| 4 | diagnostic |

이 값은 `enum RibonMode`의 native numeric value를 직렬화한 것이 아니다. Product composer는
mode spelling을 이 wire registry로 명시적으로 변환하며 C enum cast를 사용하지 않는다.

### Key usage

| Value | Usage |
| ---: | --- |
| 1 | normal Ribos policy |
| 2 | recovery Ribos policy |
| 3 | provisioning Ribos policy |
| 4 | diagnostic Ribos policy |
| 5 | update manifest |
| 6 | boot image |

Signature 하나는 usage 하나만 가진다. Usage bitmap이나 복수 역할은 message에 기록하지 않는다.
Trust-store key record는 여러 usage를 허용할 수 있지만 authorization request는 그중 하나를
정확히 선택한다.

Ribos `.rba`에는 mode와 policy usage가 동일한 번호로 대응해야 한다. Update-manifest 또는
boot-image usage로 Ribos policy를 서명하면 codec 또는 authorization이 거부한다. Update와 image
codec은 같은 registry를 사용하되 object별 별도 canonical message contract를 가져야 한다.

## Canonical 232-byte message

Message version은 `1.0`이며 정확히 232 byte다. 모든 integer는 unsigned little-endian이다.
Packed C struct, native endian, pointer, `size_t`, padding과 bit-field를 직렬화하지 않는다.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 32 | zero-padded ASCII `RIBON-TRUST-MESSAGE-V1` |
| 32 | 2 | trust-message major = 1 |
| 34 | 2 | trust-message minor = 0 |
| 36 | 2 | artifact envelope major |
| 38 | 2 | artifact envelope minor |
| 40 | 2 | VM ABI major |
| 42 | 2 | VM ABI minor |
| 44 | 2 | bytecode ISA major |
| 46 | 2 | bytecode ISA minor |
| 48 | 2 | hash algorithm, 1 = SHA-256 |
| 50 | 2 | signature algorithm, 1 = Ed25519 |
| 52 | 2 | execution mode |
| 54 | 2 | key usage |
| 56 | 8 | rollback sequence |
| 64 | 8 | executable payload byte length |
| 72 | 32 | artifact digest |
| 104 | 32 | product digest |
| 136 | 32 | schema digest |
| 168 | 32 | rollback-domain digest |
| 200 | 32 | SHA-256(key ID bytes) |

Sequence는 wrap하지 않는 u64다. Value 0은 새 domain의 genesis confirmed policy에 사용할 수
있다. Max value에서 successor를 만들 수 없으므로 update authority는 해당 domain을
fail-closed 상태로 취급한다.

Signer와 loader는 `tests/fixtures/security/ribos-policy-trust-v1.json`의 exact message bytes와
SHA-256을 공통 vector로 사용한다. C codec과 host inspector 중 하나라도 다른 byte를 만들면
signature를 생성하거나 검증하지 않는다.

## Codec 입력 검증

`ribos_artifact_trust_message_v1()`은 다음 순서로 입력을 검사한다.

1. pointer, key ID length와 context size
2. trust/envelope/VM/ISA version
3. hash와 signature algorithm
4. mode registry
5. key-usage registry
6. Ribos policy mode/usage exact match
7. flags와 reserved zero
8. artifact, product, schema와 rollback-domain identity nonzero
9. payload length의 u64 표현 가능성

첫 실패만 stable `RibosArtifactTrustStatus`로 반환한다. Unsupported version과 algorithm을
generic invalid format으로 축약하지 않는다. 실패한 output은 signature input으로 사용할 수
없다.

112-byte message를 받는 compatibility 함수, version 선택 option과 dual verification은 두지
않는다. Signature provider는 232-byte v1 message만 소비한다.

## Authorization ordering

Production product authorizer는 다음 순서를 지킨다.

1. envelope range, payload hash와 executable payload를 structural open한다.
2. product, schema, mode, usage와 rollback-domain identity를 selected product graph와 비교한다.
3. key ID를 immutable trust store의 유일한 record로 resolve한다.
4. key status, usage, product, domain, sequence range와 bounded delegation을 검사한다.
5. exact 232-byte message에 Ed25519 verification을 수행한다.
6. protected rollback state를 읽고 canonical record와 generation을 검사한다.
7. confirmed/trial sequence eligibility를 검사한다.
8. Stage-1과 Stage-2 bytecode verifier를 실행한다.
9. 모든 receipt와 copied bytes를 봉인한 뒤에만 `PreparedProgram`을 만든다.

Signature failure 전에 rollback state를 쓰지 않는다. Signature가 유효해도 product/schema/mode,
usage, key policy, rollback 또는 verifier 실패를 우회하지 못한다.

## Stable authorization failure class

Product security layer는 내부 receipt에 다음 class를 보존한다. 외부 interactive surface는
공격자에게 key 상태를 노출하지 않도록 여러 class를 하나의 authorization failure로 축약할 수
있다.

| Class | 의미 |
| --- | --- |
| `MALFORMED` | envelope, range, reserved 또는 canonical encoding 실패 |
| `UNSUPPORTED_VERSION` | trust, envelope, VM 또는 ISA version 실패 |
| `IDENTITY_MISMATCH` | artifact, product 또는 schema identity 실패 |
| `MODE_USAGE_MISMATCH` | selected mode와 single usage 실패 |
| `DOMAIN_MISMATCH` | product가 소유하지 않은 rollback domain |
| `KEY_UNKNOWN` | key ID가 unique record로 resolve되지 않음 |
| `KEY_POLICY` | usage, lifecycle, delegation 또는 sequence 범위 실패 |
| `SIGNATURE_INVALID` | cryptographic verification 실패 |
| `STATE_UNAVAILABLE` | protected-state provider를 읽을 수 없음 |
| `STATE_INVALID` | corrupt, ambiguous 또는 stale generation |
| `ROLLBACK_REJECTED` | sequence가 confirmed/trial rule을 만족하지 않음 |
| `VERIFIER_REJECTED` | Stage-1 또는 Stage-2 실패 |

첫 실패 class가 authorization receipt를 소유한다. Later stage를 실행해 더 구체적인 오류로
덮어쓰지 않는다.

## 비목표와 증거 경계

Canonical vector와 codec unit test만으로는 다음을 증명하지 않는다.

- Selected Ed25519 provider의 정확성 또는 constant-time 성질
- production public key와 private-key 보관 방식
- TPM, RPMB, secure element 또는 fuse-backed rollback state
- power-loss-safe journal과 physical flash 동작
- update manifest와 boot image codec 구현

Ed25519 equation과 strict input profile은 별도 signature-provider gate가 같은 message bytes로
검사한다. Bounded immutable key table, lifecycle, authority containment와 최대 두 edge delegation은
key-policy gate가 검사한다. Protected state, trust-store update와 hardware 항목은 독립 evidence
gate를 요구한다.
