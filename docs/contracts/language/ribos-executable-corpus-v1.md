---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/examples/
  - language/ribos/examples/tests/executable_corpus_tests.py
  - language/ribos/frontend/tests/fragments/
tests:
  - make check-ribos-executable-corpus
  - qstar test --suite //tests:ribos_executable_corpus_tests
hardware:
  - none
supersedes:
  - parser-only positive Ribos example classification
---

# Ribos executable example corpus v1

## 목적과 증거 등급

`language/ribos/examples/executable/`의 `.rbs`는 설명용 pseudo-code가 아니라 실행 가능한
공개 정책 corpus다. 각 파일은 다음 단계를 모두 통과해야 한다.

```text
UTF-8 source
  -> tracked parser snapshot
  -> typed semantic analysis
  -> Policy IR and exact resource closure
  -> deterministic unsigned .rba artifact
  -> compiler-independent Stage-1 and Stage-2 verifier
  -> production target-core VM host replay
  -> declared terminal outcome
```

`language/ribos/examples/manifest.json`은 source와 artifact digest, capability closure,
instruction/helper/stack/call-depth upper bound 및 terminal outcome을 고정한다. Artifact
emission은 같은 입력에서 두 번 수행해 byte identity를 요구하며, VM replay는 같은
artifact/context/transcript tuple로 네 번 수행해 report identity를 요구한다.

이 gate는 host-only execution evidence다. Production signature, rollback counter, firmware
integration, QEMU guest 또는 physical hardware 실행을 증명하지 않는다.

## 분류 계약

- `examples/executable/*.rbs`: 모든 compiler와 VM 단계를 닫는 public positive corpus다.
- `frontend/tests/fragments/*.rbs`: parser 표면만 검사하는 의도적인 불완전 조각이다.
- `frontend/tests/negative/*.rbs`: parser stage의 거부 corpus다.
- `frontend/tests/semantic/negative/*.rbs`: stable semantic error code를 요구하는 거부 corpus다.

`fragment`를 `positive`, `example` 또는 실행 가능 정책으로 보고해서는 안 된다. Public
executable source를 추가하면 manifest entry와 이 문서의 tagged code block을 같은
change에서 추가해야 한다.

## Executable corpus

### Bounded collections

고정 용량 `Array` 순회, `FrozenMap` lowering, mutable local과 expression condition을
검사한다. 이 entry path는 `Array` loop와 terminal helper를 실행하며 `FrozenMap` 함수는
artifact와 verifier table까지 내린다.

<!-- ribos-executable: bounded_collections -->
```text
@pure
def profile_for(revision: BoardRevision) -> Profile {
    let profiles: FrozenMap[BoardRevision, Profile, 2] = {
        BoardRevision.V1: Profile.ETH_PHY_A,
        BoardRevision.V2: Profile.ETH_PHY_B,
    }
    return profiles.get(
        revision,
        default=Profile.ETH_PHY_A,
    )
}

@policy(
    capabilities=[
        Capability.BOOT,
    ],
    instruction_budget=512,
    helper_budget=2,
)
def boot(ctx: BootContext) -> Result[BootAction, BootError] {
    let required_devices: Array[Device, 3] = [
        Device.UART0,
        Device.STORAGE0,
        Device.ETH0,
    ]
    let mut count: u32 = 0
    for dev in required_devices {
        if dev == Device.UART0 {
            count = count + 1
        }
    }
    if count == 1 {
        return Ok(boot.recovery(RecoveryReason.REQUESTED))
    }
    return Ok(boot.recovery(RecoveryReason.INVALID_IMAGE))
}
```

### Expression and control flow

정수 bitwise·shift·arithmetic precedence, boolean short-circuit와 direct call을 검사한다.

<!-- ribos-executable: expression_control_flow -->
```text
@pure
def policy_ready(ctx: BootContext) -> bool {
    let mask: u32 = ((0x10 | 0b0011) ^ 3) & 255
    let shifted: u32 = (mask << 2) >> 1
    let inverted: u32 = ~shifted
    let positive: u32 = +3
    let arithmetic: u32 = positive + 2 * 5 / 1 % 4
    return inverted > positive and arithmetic > 0
}

@policy(
    capabilities=[
        Capability.BOOT,
    ],
    instruction_budget=256,
    helper_budget=1,
)
def boot(ctx: BootContext) -> Result[BootAction, BootError] {
    if policy_ready(ctx) {
        return Ok(boot.recovery(RecoveryReason.REQUESTED))
    }
    return Err(BootError.POLICY_REJECTED)
}
```

### Minimal recovery

가장 작은 성공 정책과 terminal `BootAction` helper를 검사한다.

<!-- ribos-executable: minimal_recovery -->
```text
@policy(
    capabilities=[
        Capability.BOOT,
    ],
    instruction_budget=64,
    helper_budget=1,
)
def boot(ctx: BootContext) -> Result[BootAction, BootError] {
    return Ok(boot.recovery(RecoveryReason.REQUESTED))
}
```

### Policy reject

Helper를 호출하지 않는 fail-closed policy error terminal을 검사한다.

<!-- ribos-executable: policy_reject -->
```text
@policy(
    capabilities=[
        Capability.BOOT,
    ],
    instruction_budget=64,
    helper_budget=1,
)
def boot(ctx: BootContext) -> Result[BootAction, BootError] {
    return Err(BootError.POLICY_REJECTED)
}
```

### Result and Option

`Option` propagation, variant payload match와 `Result` terminal을 검사한다.

<!-- ribos-executable: result_option -->
```text
@pure
def maybe_attempts(enabled: bool, attempts: u32) -> Option[u32] {
    if enabled {
        return Some(attempts)
    }
    return None
}

@pure
def checked_attempts(enabled: bool) -> Option[u32] {
    let attempts = maybe_attempts(enabled, 3)?
    return Some(attempts + 1)
}

@policy(
    capabilities=[
        Capability.BOOT,
    ],
    instruction_budget=256,
    helper_budget=1,
)
def boot(ctx: BootContext) -> Result[BootAction, BootError] {
    let attempts = checked_attempts(True)
    match attempts {
        Option.Some(value) => {
            if value >= 4 {
                return Ok(boot.recovery(RecoveryReason.REQUESTED))
            }
            return Err(BootError.POLICY_REJECTED)
        }
        None => {
            return Err(BootError.POLICY_REJECTED)
        }
    }
}
```

### Typed declarations

사용자 struct, payload enum, named arguments, direct call과 exhaustive match를 검사한다.

<!-- ribos-executable: typed_declarations -->
```text
struct UpdateCondition {
    minimum_battery: u8
    require_grounded: bool
}

enum BootMode {
    Normal
    Recovery(RecoveryReason)
}

@pure
def choose_mode(ctx: BootContext) -> BootMode {
    let condition: UpdateCondition = UpdateCondition(
        minimum_battery=60,
        require_grounded=True,
    )
    if condition.minimum_battery < 60 {
        return BootMode.Recovery(RecoveryReason.LOW_POWER)
    }
    return BootMode.Normal
}

@policy(
    capabilities=[
        Capability.BOOT,
    ],
    instruction_budget=256,
    helper_budget=1,
)
def boot(ctx: BootContext) -> Result[BootAction, BootError] {
    let mode = choose_mode(ctx)
    match mode {
        BootMode.Normal => {
            return Ok(boot.recovery(RecoveryReason.REQUESTED))
        }
        BootMode.Recovery(reason) => {
            return Ok(boot.recovery(reason))
        }
    }
}
```

## 변경 규칙

Source, tagged block 또는 compiler output이 바뀌면 gate는 digest나 resource mismatch로
실패한다. 변경자는 새 값만 기계적으로 복사하지 말고 다음을 검토해야 한다.

1. Source semantics와 expected terminal outcome이 의도한 것인가.
2. Capability와 helper closure가 넓어지지 않았는가.
3. Instruction, stack 또는 call-depth 증가가 설명 가능한가.
4. Artifact ABI와 selected product schema identity가 유지되는가.
5. Negative parser와 semantic corpus의 stage/error-code 계약이 유지되는가.

검토 후 source, 문서 block과 manifest를 한 commit에서 갱신한다.
