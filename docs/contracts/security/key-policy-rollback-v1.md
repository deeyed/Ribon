---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-08-01
code_paths:
  - include/Ribon/security/key_policy.h
  - src/security/key_policy.c
  - include/Ribon/policy/ribos.h
  - language/ribos/vm/include/ribos/vm/prepared.h
  - qstar/schemas/product.schema.json
  - tools/generate_plugin_registry.py
  - include/Ribon/security/protected_state.h
  - src/security/protected_state.c
tests:
  - make check-security-key-policy
  - make check-security-key-policy-sanitizer
  - make check-security-key-policy-graphs
  - make check-ribos-r18
  - make check-security-protected-state
  - make check-security-protected-state-graphs
  - ribon-trust-message-vector-v1
hardware:
  - none
supersedes:
  - implicit product key and rollback callback semantics
---

# Product key policy와 rollback state v1 계약

## 권한 분리

Trust root, key lifecycle과 rollback floor는 product-selected security authority가 소유한다.
Ribos policy, generic VM, bytecode verifier, network transport와 OS health payload는 이 상태를
직접 변경하지 못한다.

| 책임 | 소유자 |
| --- | --- |
| root key와 trust-store identity | immutable product graph |
| signature byte 검증 | selected signature provider |
| key usage, delegation, rotation, revocation | selected key-policy provider |
| confirmed floor와 trial state | protected-state provider와 update policy |
| OS health payload 의미 | selected Boot Protocol |
| policy bytecode safety | independent Ribos verifier |

Cryptographically valid signature는 key-policy 승인이나 rollback 승인이 아니다.

## Bounded trust store

Product trust store v1은 최대 32개 key record를 가진다. Key ID는 product 안에서 unique하고
1..64 byte다. Duplicate ID, duplicate active public-key identity, cycle, unknown issuer와
상한 초과는 composition 단계에서 fail closed한다.

Key record는 최소 다음 의미를 가진다.

- key ID와 public-key identity
- 허용된 single-usage bitmap
- exact product digest
- 허용 rollback-domain digest 집합
- minimum과 maximum sequence, inclusive
- issuer key ID 또는 root marker
- delegation depth
- lifecycle state

Delegation은 root를 제외하고 최대 두 edge다.

```text
root -> intermediate -> signing leaf
```

Arbitrary certificate parser, recursive chain discovery와 network key fetch는 허용하지 않는다.
Composer가 stable key-ID 순서로 생성한 고정 table만 탐색한다.

`RibonKeyPolicyStore`는 product manifest exact-byte digest, store generation과 bounded record
table을 immutable image data로 소유한다. Runtime은 store를 승인하기 전에 다음을 독립적으로
재검산한다.

- ABI, flags, reserved와 record 수
- strict key-ID 및 rollback-domain digest 정렬
- public key의 SHA-256 identity와 nonzero identity
- lifecycle과 무관한 public-key identity 중복 부재
- issuer 존재, 선언 depth, cycle 부재와 최대 두 edge
- child의 mode, usage, domain과 sequence authority가 issuer의 부분집합인지
- pointer와 native C layout을 제외한 canonical store digest

Canonical store serialization은 little-endian이며 32-byte magic, version, record count,
generation, store-ID digest와 stable key-ID 순서의 record fields를 포함한다. Generated C의
pointer 값, padding, `size_t`와 callback 주소는 identity에 들어가지 않는다. Composer와 runtime이
같은 digest를 만들지 못하면 store 전체를 거부한다.

## Key lifecycle

Lifecycle state는 다음 의미를 가진다.

| State | Authorization 의미 |
| --- | --- |
| `ACTIVE` | usage/product/domain/sequence와 chain이 맞으면 승인 가능 |
| `RETIRING` | record가 가진 maximum sequence 이하의 기존 전환만 승인 가능 |
| `REVOKED` | 모든 mode와 sequence에서 거부 |

Wall clock은 trust decision의 정본 입력이 아니다. Activation과 retirement는 signed monotonic
sequence 범위와 immutable product trust-store generation으로 표현한다.

Rotation overlap 동안 old/new key가 모두 `ACTIVE` 또는 old key가 `RETIRING`일 수 있다. 한
artifact는 key ID 하나와 signature 하나만 가지며 두 key의 공동 승인을 요구하지 않는다.
Revocation은 usage와 sequence 허용보다 우선한다. Revoked root나 issuer 아래의 모든 descendant는
거부한다.

Root key, usage 확장, domain 추가와 revocation table은 external Ribos policy가 수정하지 않는다.
새 trust store 설치는 별도 provisioning/update authority와 더 높은 generation을 요구한다.

## Rollback domain

Rollback domain은 서로 sequence를 비교할 수 있는 object 집합이다. Stable domain ID의 UTF-8
bytes를 SHA-256해 trust message에 넣는다. 서로 다른 product가 같은 domain을 공유하려면
각 product trust graph가 동일 digest와 공동 update authority를 명시해야 한다. 이름이 비슷한
domain을 prefix 또는 case-insensitive 비교로 합치지 않는다.

Factory recovery, normal policy, update manifest와 boot image는 별도 domain을 사용할 수 있다.
Factory recovery 실행은 normal domain의 floor를 감소시키지 않는다.

## Protected state

Domain별 logical state는 다음 값을 가진다.

```text
confirmed_floor: u64
pending_sequence: none | u64
trial_attempts_remaining: u32
generation: u64
state: CONFIRMED | TRIAL
```

모든 integer는 wrap하지 않는다. `generation`은 journal record 선택을 위한 값이며 signed object의
`sequence`와 같은 의미가 아니다. Conflicting valid generation, unknown state, nonzero reserved,
counter overflow와 provider unavailable은 fail closed한다.

## Trial과 confirmation

Confirmed floor가 `N`일 때 새 pending candidate는 정확히 `N + 1`이어야 한다. Sequence skip은
허용하지 않는다.

### Trial 개시

1. Candidate의 product/schema/mode/usage/domain binding과 signature를 검증한다.
2. Candidate sequence가 `N + 1`인지 확인한다.
3. Stage-1과 Stage-2 verifier를 통과한다.
4. `pending_sequence=N+1`, bounded positive attempt count를 새 generation에 기록한다.
5. Write, flush와 readback 뒤 commit된 record만 trial authority가 된다.

### Trial authorization

`TRIAL(N, N+1)`에서는 다음 두 object만 실행 가능하다.

- pending `N+1`: attempt count가 남아 있는 trial boot
- confirmed `N`: pending failure 뒤 fallback을 위한 confirmed boot

`N-1`, `N+2`와 domain이 다른 object는 서명이 유효해도 거부한다. Trial attempt를 시작하면
durable metadata에 attempt 감소가 commit된 뒤에만 payload transfer를 시작한다.

### Confirmation

Boot Protocol health payload가 product, protocol ID, slot, generation, nonce와 pending sequence를
정확히 확인하면 별도 atomic transition으로 다음 state를 만든다.

```text
confirmed_floor = N + 1
pending_sequence = none
trial_attempts_remaining = 0
state = CONFIRMED
```

Confirmation commit 뒤 normal authority는 `N`을 거부한다. 기능상 downgrade가 필요해도 더 큰
sequence를 가진 새 signed object를 발행해야 한다.

### Trial 실패

Attempt가 소진되거나 policy-defined terminal failure가 확인되면 pending state를 제거하고
confirmed floor `N`을 유지한다. Failed `N+1` byte가 storage에 남아 있어도 execution authority가
아니다. Factory recovery는 failure receipt를 처리할 수 있지만 `N`보다 작은 normal policy를
승인하지 못한다.

## Persistent ordering

Protected state write는 다음 순서를 바꾸지 않는다.

```text
prepare new generation
  -> write complete record
  -> flush
  -> read back and validate
  -> commit generation selector
  -> flush
```

Partial, staging 또는 readback-invalid record는 authorization state가 아니다. 두 record와 두
selector의 exact codec, commit ordering과 fault-injection 범위는
{doc}`protected-state-journal-v1` 계약이 소유한다.

## Policy가 할 수 없는 일

Ribos helper와 terminal action은 다음 authority를 얻지 못한다.

- root/public key 추가 또는 교체
- key usage 확장
- revocation 해제
- delegation depth 증가
- product 또는 rollback-domain digest 변경
- confirmed floor 감소
- pending sequence 임의 선택
- verifier 또는 signature provider 우회
- protected-state raw address 또는 record byte 접근

Policy는 product가 미리 승인한 candidate 중 선택하거나 trial/recovery intent를 반환할 수 있다.
최종 key, signature와 rollback decision은 native product authority가 다시 검사한다.

## Native authorization ABI

`ribon_key_policy_authorize()`는 key ID를 exact record로 resolve한 뒤 product, mode, single usage,
rollback domain, sequence, lifecycle과 issuer chain을 검사한다. 성공 결과인
`RibonKeyPolicyDecision`에는 record index, delegation depth, store generation, key identity와
store digest만 들어간다. Public key, store pointer와 provider callback은 decision에 포함하지
않는다.

`ribon_key_policy_verify()`는 위 authorization을 먼저 수행하고 성공한 exact record의 public
key만 selected signature provider에 전달한다. Policy failure에서는 cryptographic callback을
호출하지 않는다. 이 helper의 성공은 protected rollback state, bytecode verifier 또는 boot
transaction 성공을 뜻하지 않는다.

## 증거 경계

Key-policy unit, manifest negative corpus와 final object-graph gate는 bounded immutable trust-store,
rotation/revocation/delegation authorization 및 실제 Ed25519 호출 순서의 host/compile-only 증거다.
이는 protected-state provider, journal, trust-store update transaction, private-key custody 또는
physical rollback protection의 실행 증거가 아니다. Host file이나 memory provider는 state-machine
unit 및 power-cut simulation에는 사용할 수 있지만 hostile replay-safe hardware claim을 열지 않는다.
