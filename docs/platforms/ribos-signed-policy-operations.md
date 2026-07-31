---
doc_type: reference
status: accepted
authority: informative
last_verified: 2026-08-01
code_paths:
  - language/ribos/
  - tools/ribosc.c
  - tools/sign_ribos_policy.py
  - tools/generate_plugin_registry.py
  - include/Ribon/policy/ribos.h
tests:
  - make check-ribos-production-policy
  - make check-ribos-release-reproducibility
hardware:
  - none
supersedes:
  - ad hoc Ribos policy deployment procedure
---

# Signed Ribos policy 운영 지침

## 목적

이 지침은 product integrator가 `.rbs` source를 signed `.rba`로 만들고 A/B trial, confirmation,
fallback과 key rotation을 운용할 때 지켜야 할 trust direction을 설명한다. 명령 예시는 validation
product를 대상으로 하며 production key ceremony나 특정 secure-storage 제품을 정의하지 않는다.

## Authority 분리

```text
developer source (.rbs)
  -> host compiler and independent verifier
  -> unsigned artifact (.rba)
  -> offline signing authority
  -> product-bound signed artifact
  -> inactive policy slot
  -> native protected-state trial
  -> external OS/product health receipt
  -> native confirmation
```

Target의 Ribos VM은 key 추가, rollback floor 감소, trial confirmation 또는 factory policy 변경 권한을
갖지 않는다. Product graph는 allowed key, usage, mode, rollback domain, schema와 helper contract를
immutable generated binding으로 소유한다.

## Build와 서명

1. Product manifest에서 stable product ID, schema provider, helper set, capability, resource limit,
   key-policy generation과 rollback domain을 선택한다.
2. `ribosc`로 `.rbs`를 unsigned `.rba`로 compile한다. Source example은 semantic check와 artifact
   generation까지 executable corpus gate를 통과해야 한다.
3. 독립 artifact verifier로 header, section, CFG, type, capability, bounded resource와 terminal action을
   검사한다.
4. Private key가 있는 offline host에서 `tools/sign_ribos_policy.py`를 실행한다. Signer 입력은 exact
   product manifest, key ID, usage, mode, rollback domain과 strictly increasing sequence다.
5. Signed artifact와 product package의 hash, source revision, schema digest, key-policy digest와 rollback
   domain digest를 release manifest에 보존한다.

Validation 절차의 canonical entry는 다음과 같다.

```sh
make check-ribos-production-policy
make check-ribos-release-reproducibility
```

첫 gate는 trust pipeline과 QEMU runtime을 집계한다. 둘째 gate는 서로 독립된 clean build root 두 곳의
canonical release output을 byte 단위로 비교한다.

## Trial 설치와 confirmation

1. Update authority는 signed artifact를 inactive policy slot에 exact write하고 다시 읽어 hash와
   signature를 확인한다.
2. Native adapter는 candidate의 Stage-1/2 verifier preflight가 끝난 뒤에만 pending sequence와 attempt
   budget을 durable journal에 기록한다.
3. 각 trial boot 전에 attempt를 먼저 감소시키고 durable commit한다. VM 성공만으로 confirmed 상태를
   쓰지 않는다.
4. OS 또는 product health authority가 별도 authenticated receipt를 검증한 뒤 exact pending sequence를
   `ribon_ribos_policy_confirm()`에 전달한다.
5. Attempt 소진, action rejection 또는 health failure는 `ribon_ribos_policy_fail_trial()`로 pending을
   제거한다. Confirmed floor와 이전 good artifact는 보존한다.
6. External policy를 열거나 verifier가 실행되기 전의 실패도 compiled factory recovery로 닫혀야 한다.

Factory recovery는 `.rba` slot, network 또는 mutable policy state에 의존하지 않는 native callback이다.
Recovery가 실행되었다는 사실은 원래 policy 성공으로 기록하지 않는다.

## Key rotation

Key rotation은 policy script가 아니라 product release와 protected-state authority의 작업이다.

1. 새 public key를 bounded key-policy generation에 추가하되 기존 confirmed artifact를 검증할 overlap
   window를 명시한다.
2. Product graph와 firmware image를 다시 생성하고 key-policy digest를 release identity에 기록한다.
3. 새 key로 더 높은 sequence의 trial artifact를 서명한다.
4. Health-confirmed rollout 뒤 구 key를 다음 immutable product generation에서 제거한다.
5. Revocation은 rollback floor 감소나 unknown key 허용으로 구현하지 않는다.

Target image와 repository에는 production private key를 넣지 않는다. Production signer, HSM ceremony,
key escrow, fleet approval와 revocation distribution은 배포자가 별도 운영 계약으로 닫아야 한다.

## 장애 분류와 보존할 증거

Operator는 최소한 artifact/product/schema/key/sequence/state/verifier/VM/action/transaction failure를
구분해 기록한다. Receipt에는 pointer, private material과 secret을 넣지 않는다. QEMU 또는 device별로
다음 evidence를 보존한다.

- signed artifact와 composed image의 실행 전후 hash
- product manifest, schema, key-policy와 rollback-domain digest
- protected-state provider identity와 비밀이 없는 transition receipt
- serial/log hash, exact marker 수와 순서
- process cleanup, timeout과 forced-kill 여부
- source revision, toolchain와 firmware dependency identity

## Claim 경계

`check-ribos-production-policy` 성공은 host/unit, clean-build와 QEMU runtime evidence를 결합한다. Physical
hardware, production private-key custody, hardware anti-replay, hostile power-loss, real update transport,
flash atomicity, OS health service 또는 fleet rollout 성공은 각각 별도 evidence가 필요하다.
