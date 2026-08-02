---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-08-02
code_paths:
  - include/Ribon/security/protected_state.h
  - src/security/protected_state.c
  - src/security/sha256.c
  - qstar/schemas/product.schema.json
  - tools/generate_plugin_registry.py
tests:
  - make check-security-protected-state
  - make check-security-protected-state-sanitizer
  - make check-security-protected-state-graphs
hardware:
  - none
supersedes:
  - implicit rollback counter storage semantics
---

# Protected rollback-state journal v1 계약

## 목적과 권한 경계

Protected state는 architecture monotonic timer와 별개의 security authority다. Timer는 deadline과
watchdog 진행을 측정하지만 signed object sequence의 floor를 보존하지 않는다. Protected-state
provider는 domain별 confirmed floor, pending trial, 남은 attempt와 journal generation을 보존한다.

Generic engine은 provider의 물리 매체, flash address, TPM command, RPMB frame과 secure-element
protocol을 알지 않는다. Product graph가 exact provider symbol과 rollback domain을 선택한다.
Ribos VM은 provider pointer, journal byte, raw address와 floor mutator를 받지 않는다.

Provider class의 의미는 다음과 같다.

| Class | 허용하는 claim |
| --- | --- |
| `HARDWARE` | 해당 product가 별도 검증한 hardware anti-replay 및 durability 범위 |
| `REFERENCE` | codec, ordering, state-machine과 fault-injection 실행 |
| `FIXTURE` | 단위 시험을 위한 의도적 제한 또는 failure injection |

`REFERENCE` 또는 `FIXTURE`를 선택한 graph는 hostile media replay, physical power-loss durability,
TPM, RPMB 또는 secure-element 보장을 주장하지 않는다.

## Product binding

Signed product는 signature provider, immutable key policy와 protected-state provider를 함께
선택한다. 하나만 없거나 class가 허용되지 않으면 composition을 거부한다. Protected provider의
rollback domain 집합은 모든 key-policy record가 승인하는 domain의 합집합과 exact match해야 한다.

Domain ID는 1..128 byte non-NUL UTF-8이고 source manifest에서는 정렬·중복 없는 목록이다.
Generated binding은 각 ID의 SHA-256 digest를 byte 순으로 정렬해 저장한다. Runtime journal은 exact
32-byte digest만 선택하며 prefix, case folding 또는 product 이름 추론을 하지 않는다.

Provider callback은 `(provider, domain digest, logical object, slot, exact bytes)`를 받는다. Logical
object는 `RECORD`와 `SELECTOR`뿐이다. 한 provider가 여러 domain을 선택하면 provider가 digest를
독립 namespace로 처리해야 한다.

## Logical state

Selector로 commit된 한 domain state는 다음 tuple이다.

```text
kind: CONFIRMED | TRIAL
confirmed_floor: u64
pending_sequence: 0 | confirmed_floor + 1
trial_attempts_remaining: u32
generation: positive u64
domain_digest: [u8; 32]
trial_binding_digest: zero | [u8; 32]
attempt_sequence: 0 | positive u64
```

`CONFIRMED`는 pending과 attempts가 모두 0이다. Legacy/unbound history는 binding과 attempt가
모두 0이고, bound confirmation 또는 failed bound trial은 마지막 binding과 positive attempt
sequence를 유지한다. Confirmed duplicate는 floor까지 exact match할 때만 승인되며 failed receipt는
floor가 다르므로 stale이다. `TRIAL`은 pending이 exact successor이며 attempts는 0..32다. Unbound
trial은 binding/attempt가 모두 0이고 bound trial은 둘 다 nonzero다. Attempt 0은 직전 trial transfer가
마지막 attempt를 소비했다는 durable state이고 다음 pending authorization을 거부한다. 모든 reserved
byte, flags, enum과 size/version은 exact해야 한다.

Sequence와 generation은 서로 다른 값이다. Sequence는 signed object rollback ordering이고
generation은 journal transaction ordering이다. 둘 중 어느 값도 wrap하지 않는다.

## Wire codec

Native C struct를 저장 매체에 쓰지 않는다. Record와 selector는 각각 exact 160-byte
little-endian wire object다. Pointer, `size_t`, padding, callback 주소와 native enum layout은
serialization에 포함되지 않는다.

Record는 다음 의미를 포함한다.

- 16-byte magic과 format 1.0
- exact byte size와 `PREPARED` phase
- logical state, generation, confirmed floor, pending sequence와 attempts
- exact domain digest
- exact boot-attempt binding digest와 domain-monotonic attempt sequence
- header/payload의 CRC32C와 zero reserved tail

Selector는 다음 의미를 포함한다.

- 16-byte magic과 format 1.0
- selected record slot과 generation
- exact domain digest
- selected 160-byte record의 SHA-256
- selector CRC32C와 zero reserved tail

CRC32C는 torn/corrupt byte 검출을 위한 codec checksum이다. SHA-256은 selector가 exact record
bytes를 지시하게 한다. 이 둘만으로 hostile replay resistance를 주장하지 않으며 그 성질은
provider class와 product-specific hardware evidence가 소유한다.

## Redundant commit protocol

Record slot과 selector slot은 각각 두 개다. Commit selector가 가리키지 않는 slot에 다음 generation을
준비한다. Commit 순서는 다음과 같다.

```text
encode next PREPARED record
  -> write inactive record object
  -> flush domain
  -> read back exact record bytes
  -> decode, checksum, SHA-256와 expected bytes 검증
  -> write inactive selector object
  -> flush domain
  -> reopen both selector와 record slot
  -> exact new generation 확인
```

Selector flush 전의 새 record는 authorization state가 아니다. 두 selector가 유효하면 더 큰
generation을 선택한다. 같은 generation의 서로 다른 valid selector 또는 record는 conflict로
거부한다. 가장 높은 valid selector가 가리키는 record가 invalid이면 더 오래된 selector로
authorization을 낮추지 않고 corruption으로 거부한다.

Inactive selector의 partial write가 checksum을 만족하지 않으면 기존 selector가 authority를
유지한다. 새 selector의 의미 있는 byte와 checksum이 모두 durable해지면 write/flush callback이
failure를 보고했더라도 재부팅 후 새 state가 authority일 수 있다. 따라서 caller는 ambiguous
failure 뒤 임의 재시도나 payload transfer를 하지 않고 journal을 다시 열어 commit된 generation을
확인한다.

## State transition

### Initialize

완전히 비어 있는 두 selector에서만 generation 1의 `CONFIRMED(N)`을 provision한다. 이미 valid
authority가 있으면 floor가 더 작더라도 initialize로 덮어쓰지 않는다. Corrupt 또는 unavailable
journal도 uninitialized로 취급하지 않는다.

### Begin trial

입력 state가 `CONFIRMED(N)`일 때만 `N+1`과 1..32 attempts를 받아 `TRIAL(N,N+1)`을 commit한다.
`N`, `N+2`, overflow와 이미 열린 trial은 거부한다.

Update 경로에서는 {doc}`../update/transaction-journal-v1`의 exact target identity가 `PENDING(N+1)`으로
commit된 뒤에만 begin-trial을 호출한다. Update journal의 PENDING만으로 transfer하지 않으며,
protected-state commit 전에 crash가 나면 candidate는 inert pending으로 남는다. Retry는 동일 identity의
trial을 열어야 하고 confirmed floor를 낮추어서는 안 된다.

`begin_bound_trial()`은 같은 successor 규칙에 더해 nonzero canonical binding digest와 positive
attempt sequence를 commit한다. Binding은 generic confirmation engine이 product, protocol/version,
slot, image generation, manifest digest/sequence, policy version, nonce와 attempt sequence에서 계산한다.
Journal은 이 raw identity를 해석하거나 저장하지 않는다.

이미 열린 bound trial에서 다음 boot를 시도할 때 `rebind_trial_attempt()`은 pending sequence와 남은
attempt를 유지하면서 더 큰 attempt sequence와 새 binding만 commit한다. 같은/작은 attempt,
unbound/bound 혼용과 exhausted trial의 재결속은 거부한다.

### Authorize와 consume

`CONFIRMED(N)`은 `N`만 승인한다. `TRIAL(N,N+1)`은 fallback용 `N`과 attempts가 남은 `N+1`만
승인한다. Pending payload transfer 전에 `consume_trial_attempt()`가 attempts 감소를 새 journal
generation으로 commit해야 한다. Pure authorization 결과만으로 transfer하지 않는다.

### Confirm과 fail

`confirm(N+1)`은 unbound exact pending을 새 confirmed floor로 올리고 pending/attempts를 0으로 만든다.
`confirm_bound()`은 sequence, binding과 attempt sequence가 모두 일치하는 bound trial만 승격한다.
이미 같은 bound identity로 confirmed된 상태에는 write 없이 성공해 receipt 재전달을 idempotent하게
처리하며, 다른 binding이나 stale attempt는 `BINDING_MISMATCH`다.
이후 `N`은 normal domain에서 rollback이다. `fail_trial()`은 pending을 제거하되 confirmed floor
`N`을 유지하면서 마지막 bound attempt history를 보존해 다음 attempt sequence 재사용을 막는다.
Recovery domain의 transition은 provider namespace와 exact binding 때문에 normal
domain state를 수정하지 않는다.

## Fail-closed 조건

다음 조건은 authorization을 반환하지 않는다.

- provider 또는 domain unavailable
- selector/record magic, version, size, enum, reserved 또는 checksum 오류
- selected record SHA-256 불일치
- 같은 generation의 conflicting valid object
- domain digest mismatch
- bound/unbound pairing 또는 boot-attempt binding mismatch
- sequence skip 또는 rollback
- exhausted trial attempt
- sequence/generation overflow
- record readback mismatch

Host/reference provider의 media snapshot 전체를 과거 snapshot으로 되돌리는 hostile replay는
software journal만으로 구분할 수 없다. Production product는 monotonic authenticated storage를
제공하는 `HARDWARE` provider와 별도의 physical evidence가 필요하다.
