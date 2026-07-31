---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - include/Ribon/policy/ribos.h
  - src/plugins/policy/ribos/adapter.c
  - language/ribos/vm/
  - tools/generate_plugin_registry.py
  - qstar/schemas/product.schema.json
  - qstar/manifests/host-reference.json
  - tests/policy/ribos_integration_tests.c
tests:
  - make check-ribos-ribon-integration
  - make check-ribos-product-graphs
  - make check-ribos-normal-no-network
  - make check-ribos-factory-recovery
  - make check-ribos-vm
  - make check-ribos-r18
  - make check-object-graphs
  - make check
  - make docs
hardware:
  - none
supersedes:
  - handwritten Ribos product embedding
---

# Ribos와 Ribon product 통합 v1 계약

## 목적과 계층

이 계약은 verified `.rba`를 Ribon의 generated product graph, typed service와 기존
boot transaction에 연결하는 경계를 고정한다.

```text
product manifest
  -> generated schema + helper contract + service route + limit
  -> generic Ribon Ribos adapter
  -> architecture-neutral Ribos target core
  -> sealed terminal outcome
  -> product action validation
  -> existing boot transaction commit + quiesce
```

의존 방향은 단방향이다.

| 계층 | 알아도 되는 것 | 금지 |
| --- | --- | --- |
| Ribos target core | artifact, selected schema, helper execution ABI | Ribon header, service ID, boot transaction |
| Ribon Ribos adapter | Core arena, generated binding, typed service, transaction | board/OS 이름, raw MMIO/flash, transfer |
| Product semantics | stable helper 의미, signature/rollback, action validation | VM opcode dispatch, Core arena layout |
| Environment service | typed native operation | policy AST, helper schema 변경 |

## Generated binding

`RibonRibosProductBinding`은 같은 product manifest에서 다음을 생성한다.

- product와 policy stable ID
- versioned schema provider와 schema digest
- stable ID 순서의 helper execution contract
- callback 주소를 제외한 canonical helper-contract digest
- helper별 exact service kind, service ID와 Ribon capability
- Ribos capability, allowed mode와 exact boot phase
- instruction, helper, stack, arena, I/O, operation, poll, duration, call-depth,
  handle와 trace limit
- monotonic timer와 optional required watchdog service
- product authorizer, factory recovery와 BootAction validator

Route와 helper binding은 stable ID가 strictly increasing해야 하며 두 table은 같은
ID와 수량을 가져야 한다. Adapter는 generated digest를 다시 계산하고 artifact가
요구한 schema digest와 함께 검사한다.

## 실행 전 검증

Adapter는 어떤 policy callback보다 먼저 다음을 fail-closed로 검사한다.

1. selected plugin이 exact `policy.ribos` descriptor인가
2. binding ABI, product ID, policy ID와 product mode가 일치하는가
3. phase가 `BOOT`이고 모든 helper가 같은 mode와 phase만 허용하는가
4. schema와 helper-contract digest가 canonical encoding과 일치하는가
5. 각 service route가 exact typed descriptor, capability와 deadline budget을
   만족하는가
6. product의 allowed capability가 route requirement를 포함하는가
7. arena와 runtime limit이 plugin/product budget 이내인가
8. normal mode에 network, flash 또는 update writer authority가 없는가
9. required timer와 watchdog operation ABI가 유효한가

Normal graph의 금지는 callback을 호출하지 않는다는 동적 약속이 아니라 manifest,
generated source와 linked object graph의 부재로 증명한다.

## Authorization과 arena

Product authorizer는 signature, key identity, rollback counter와 product trust policy를
소유한다. VM authorizer ABI는 receipt만 소비하며 generic adapter는 key format이나
secure storage를 해석하지 않는다.

Core가 제공한 `RibonArena`는 다음 순서로만 증가한다.

```text
authorization workspace
  -> copied PreparedProgram
  -> runtime storage
  -> process-local handle entry table
```

각 allocation은 alignment, overflow와 binding arena budget을 검사한다. 실패 뒤 arena를
rewind하거나 같은 request를 재실행하지 않는다. PreparedProgram은 copied artifact,
helper binding, schema identity와 effective limit을 봉인한 뒤에만 executable하다.

## 시간과 watchdog

Adapter는 selected monotonic-timer service의 tick frequency를 nanosecond receipt로
변환한다. v1 adapter는 정확한 정수 변환을 위해 1 GHz 이하 frequency만 받는다.

`watchdog_required` binding은 실행 전에 product deadline으로 watchdog을 arm해야 한다.
Missing/invalid watchdog나 arm failure는 policy byte를 실행하기 전에 factory recovery로
닫힌다. Watchdog arm은 VM 성공이나 boot 성공 증거가 아니며 commit/quiesce failure도
별도로 처리한다.

## Helper dispatch

VM은 stable helper ID만 요청한다. Adapter dispatcher는 generated sorted route에서
exact entry를 찾고, 필요한 경우 service directory에서 exact kind와 ID를 다시 찾은
뒤 product semantic callback을 동기적으로 호출한다.

Callback은 `RibosVmHelperCall`의 typed input/output, operation, poll, journal과 handle
API만 사용한다. Raw pointer 값, MMIO address, flash address, function pointer와
arbitrary jump는 policy value나 callback result로 만들 수 없다. Callback 반환 뒤
borrowed service와 call pointer는 보존하지 않는다.

## Terminal outcome와 transaction

VM outcome은 기존 terminal 계약대로 `BootAction`, `PolicyError`, `VmFault` 중 하나다.

`BootAction` 경로는 다음 순서를 바꾸지 않는다.

1. Product `validate_boot_action`이 payload, type, selected transaction과 product
   의미를 다시 검사한다.
2. `ribos_vm_boot_action_consume_v1`이 sealed action을 정확히 한 번 consume한다.
3. `ribon_boot_transaction_commit_attempt`가 persistent metadata와 flush를 완료한다.
4. `ribon_boot_transaction_quiesce_environment`가 selected environment를 닫는다.
5. Adapter는 `COMPLETE` receipt를 반환하고 transfer는 caller에 남긴다.

Action rejection 전에는 consume하지 않는다. Consume 뒤 commit 또는 quiesce가
실패하면 action을 재사용하거나 policy를 재실행하지 않고 pointer-free failure
receipt로 factory recovery를 호출한다.

## Factory recovery

Factory recovery는 external `.rba`, VM storage와 source parser 없이 호출 가능한
compiled product callback이다. Adapter failure receipt는 pointer와 secret을 포함하지
않고 stage, stable status, VM fault, action-consume 여부, transaction stage, arena
사용량과 context generation만 가진다.

Authorization, verification, missing service, VM fault, explicit PolicyError, action
rejection, commit과 quiesce failure는 request당 recovery callback을 최대 한 번
호출한다. VM 내부 fault callback과 adapter callback이 같은 product callback을
중복 호출하지 않도록 adapter는 VM terminal receipt를 관찰한 뒤 외부 통지를 한 번만
봉인한다.

Factory recovery 성공은 원래 policy outcome을 성공으로 바꾸지 않는다. Recovery
callback은 request, artifact, action 또는 VM receipt pointer를 보존하거나 같은 arena에
재진입할 수 없다.

## Mode 경계

| Mode | 허용되는 Ribos product binding |
| --- | --- |
| normal | inspect, verified boot intent, attempt commit; network/flash 금지 |
| recovery | 별도 signed graph에서 selected recovery transport와 destination만 허용 |
| provisioning | physical-presence/trust contract가 선택한 별도 graph만 허용 |
| diagnostic | evidence 전용 helper와 별도 product identity |

v1 실행 증거 class는 normal host-reference graph와 이를 기반으로 생성한 별도
`ribos-qemu-validation` diagnostic product다. Recovery와 provisioning table은
authority 규칙이며 구현 또는 실행 증거가 아니다.

## 증거 경계

다음 gate는 host object와 transaction integration evidence다.

```sh
make check-ribos-ribon-integration
make check-ribos-product-graphs
make check-ribos-normal-no-network
make check-ribos-factory-recovery
make check-object-graphs
make check-ribos-vm
```

Positive fixture는 real compiler가 만든 artifact를 generated binding으로 실행하여
semantic helper, single action consume, metadata write/flush, quiesce와 watchdog arm을
검사한다. Negative fixture는 unsigned rejection, corrupt artifact, schema mismatch,
instruction/deadline fault, action rejection, post-consume commit failure와 missing
external artifact recovery를 검사한다.

별도 `make check-ribos-r18`은 같은 artifact와 generated binding 의미를 AMD64,
AArch64와 RISC-V 64 QEMU guest에서 실행한다. 이 추가 증거도 production signature,
rollback secure storage, recovery network/flash, QEMU OS transfer 또는 physical
hardware 실행을 증명하지 않는다.
