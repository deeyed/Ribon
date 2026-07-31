---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - include/Ribon/security/signature.h
  - include/Ribon/security/ed25519.h
  - src/security/signature.c
  - src/plugins/security/ed25519/provider.c
  - tools/generate_plugin_registry.py
  - src/security/security.qst
  - src/plugins/plugins.qst
  - third_party/monocypher/4.0.3/
tests:
  - make check-security-ed25519-provider
  - make check-security-ed25519-sanitizer
  - make check-security-ed25519-cross-compile
  - make check-security-provider-graphs
hardware:
  - none
supersedes:
  - synthetic signature-byte validation
---

# Signature provider v1 계약

## 책임 경계

Signature provider는 immutable public key, message와 signature의 cryptographic equation만
검증한다. Artifact parser, product identity, key ID lookup, key usage, revocation, rollback
state와 bytecode verifier는 provider의 책임이 아니다.

```text
canonical object message
  + key-policy가 선택한 public key
  + artifact signature
  -> generic signature provider ABI
  -> strict Ed25519 provider
  -> cryptographic valid 또는 stable fail-closed status
```

Firmware surface는 verification-only다. Signer, private key derivation, private-key storage와
OpenSSL process 실행은 target source와 ABI에 없다. Offline host signer는 canonical message와
selected source product manifest exact bytes를 입력으로 사용하고 signed artifact만 release
pipeline에 전달한다.

## Provider descriptor ABI

`RibonSignatureProvider`는 magic, exact struct size, ABI version, class, algorithm, stable ID,
public-key/signature 크기, workspace 크기·정렬과 callback을 가진다. Flags와 reserved는 0이다.
알 수 없는 version, algorithm, class 또는 nonzero extension은 callback 전에 거부한다.

`PRODUCTION`과 `FIXTURE` class는 분리된 graph identity다. Production image manifest는 정확히
하나의 production provider를 선택하며 generated registry의 provider pointer와 product source
digest를 함께 소비한다. Provider가 없는 generic/library product는 null selection을 가질 수
있지만 signed object를 승인할 수 없다.

Generic `signature.h`는 concrete provider나 generated product composer를 선언하지 않는다.
Monocypher descriptor는 provider별 `ed25519.h`가, selected-provider getter는 plugin registry가
소유한다.

Request pointer의 수명은 한 callback 동안이다. Provider는 pointer를 보존하거나 mutable global
state를 변경하지 않는다. `workspace_bytes > 0`이면 caller가 크기와 정렬을 만족하는 storage를
소유한다. Monocypher 4.0.3 provider는 `workspace_bytes == 0`이고 heap 없이 bounded native stack만
사용하므로 request workspace도 null/0이어야 한다.

## Strict Ed25519 profile

Public key와 signature는 각각 정확히 32 byte와 64 byte다. Message는 bounded object codec가
소유하며 empty message에만 null pointer가 허용된다. Provider는 다음 순서로 검사한다.

1. Public key `A`와 signature point `R`의 compressed-y encoding이 field modulus보다 작은지
   검사한다.
2. Sign bit를 제외한 `A`와 `R`이 strict low-order blacklist에 속하지 않는지 검사한다.
3. Monocypher `crypto_ed25519_check`로 Ed25519 equation과 canonical scalar `S < L`을 검사한다.

Non-canonical point, low-order point와 invalid scalar는 fail closed한다. Compatibility mode,
consensus encoding, alternate equation과 dual verification은 두지 않는다.

## Stable 결과

| Status | 의미 |
| --- | --- |
| `OK` | Strict profile과 Ed25519 equation이 모두 유효함 |
| `INVALID_PROVIDER` | Descriptor ABI, class 또는 callback이 유효하지 않음 |
| `INVALID_ARGUMENT` | Request shape, size, pointer, reserved 또는 workspace 실패 |
| `UNSUPPORTED_ALGORITHM` | Request와 provider algorithm 불일치 |
| `INVALID_ENCODING` | Public point canonical/low-order filter 실패 |
| `INVALID_SIGNATURE` | Ed25519 equation 또는 scalar 실패 |
| `WORKSPACE_TOO_SMALL` | Caller-owned workspace capacity 실패 |

상위 authorizer는 외부 attack surface에서 이 값을 하나의 signature authorization failure로
축약할 수 있다. 내부 receipt는 첫 stable class를 보존하고 실패 뒤 key policy, rollback write나
VM prepare를 실행하지 않는다.

## Product와 build closure

Selected provider metadata는 source product manifest에 algorithm, class, ID와 descriptor symbol을
정확히 기록한다. Composer는 manifest exact bytes의 digest와 provider pointer를 같은 generated
registry에 낸다. QStar security suite와 Make gate는 다음을 검사한다.

- AMD64, AArch64, RISC-V64에서 freestanding compile
- 세 target map에 verify equation, wrapper와 selected descriptor가 존재
- final target image에 upstream signer symbol, host signer path와 fixture provider가 없음
- test-only 32-byte private seed가 final image에 없음
- RFC 8032 vector, independent OpenSSL output, malformed/low-order/non-canonical input 거부
- message 232 byte, public key 32 byte와 signature 64 byte의 모든 single-byte mutation 거부

## Timing과 증거 경계

Monocypher 4.0.3의 upstream constant-time claim과 verification timing fix는 vendored provenance에
고정한다. Ribon의 canonical/low-order filter는 secret을 입력받지 않는 public-input early reject다.
Provider는 private key를 처리하지 않지만 target 전체의 cache, compiler, fault-injection 및
physical timing non-interference를 증명하지 않는다.

Unit, sanitizer와 compile-only evidence는 production key custody, key-policy authorization,
revocation, protected rollback counter, secure boot chain, physical board와 인증된 side-channel
resistance를 증명하지 않는다.
