---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/vm/include/ribos/vm/runtime.h
  - language/ribos/vm/include/ribos/vm/prepared.h
  - language/ribos/vm/include/ribos/vm/storage.h
  - language/ribos/vm/include/ribos/vm/interpreter.h
  - language/ribos/vm/src/prepared.c
  - language/ribos/vm/src/runtime/storage.c
  - language/ribos/vm/src/runtime/interpreter.c
  - language/ribos/vm/tests/runtime_contract_tests.c
  - language/ribos/vm/tests/check_runtime_header.py
  - Makefile
tests:
  - make check-ribos-runtime-contract
  - make check-ribos-prepared-program
  - make check-ribos-runtime-storage
  - make check-ribos-vm-scalar
  - make check-ribos-vm-calls
  - make check-ribos-vm-loops
  - make check-ribos-schema
  - make check-ribos-verifier
  - make check-ribos-host-boundary
  - make check
  - make docs
hardware:
  - none
supersedes:
  - implicit Ribos runtime and embedder ABI
---

# Ribos VM runtime와 helper execution v1 계약

## 목적과 신뢰 경계

이 계약은 verified artifact와 실제 interpreter 사이의 process-local C ABI를 고정한다.
Source language, bytecode wire, product authorization과 Ribon service integration은 이
ABI의 책임이 아니다.

```text
authorized immutable artifact
  + selected product schema
  + product-generated helper execution contract
  + effective runtime limits
        |
        v
opaque PreparedProgram
        |
        v
Ribos VM + immutable context + embedder callbacks
        |
        +--> BootAction
        +--> PolicyError
        `--> VmFault -> factory recovery notification
```

Development machine에서 compiler와 CLI를 실행하는 계층은 `host`다. Target에서 clock,
helper와 factory recovery callback을 제공하는 계층은 `embedder`다. Embedder는 Ribon
product일 수 있지만 VM Core는 Ribon, firmware, architecture, board와 OS header를
포함하지 않는다.

## 독립 version

다음 version은 서로 독립적이다.

| Contract | v1 value | 변경 사유 |
| --- | --- | --- |
| Artifact envelope | 1.0 | signature envelope와 file range |
| Bytecode VM ABI | 1.0 | payload table와 value 의미 |
| Bytecode ISA | 1.0 | opcode와 operand encoding |
| Runtime C ABI | 1.0 | context, limit, outcome와 embedder shape |
| Helper execution contract | 1.0 | target callback의 bounded execution 의미 |
| Product semantic schema | 1.1 | type, helper signature, ownership과 capability |

Runtime reader는 major와 minor가 exact supported value가 아니면
`unsupported-runtime-abi`, helper execution reader는 `unsupported-helper-abi`로
거부한다. Structure `size`, unknown flag와 모든 reserved field도 실행 전에 fail
closed한다. 한 version을 올렸다는 이유로 다른 version을 암묵적으로 올리지 않는다.

## Process-local C ABI 규칙

`ribos/vm/runtime.h`의 public structure는 serialization format이 아니다.

- Integer field는 `uint16_t`, `uint32_t` 또는 `uint64_t`다.
- Native enum, `long`, `size_t`, bit-field와 packed structure를 field로 쓰지 않는다.
- Pointer와 callback은 process-local opaque pointer로만 사용한다.
- Pointer를 artifact, digest 또는 receipt byte로 직렬화하지 않는다.
- Public ABI에 Ribon service descriptor, firmware handle, MMIO address와 OS object를
  넣지 않는다.
- `RibosPreparedProgram`과 `RibosVmHelperCall`은 incomplete type이다. Caller는
  `sizeof`나 field access 대신 versioned query/accessor만 사용한다.

Structure의 `size`는 같은 process에서 compile된 descriptor 검사용이다. Wire layout,
architecture 간 binary interchange 또는 stable disk representation을 뜻하지 않는다.

## Stable registry

Runtime status와 ASCII spelling은 다음과 같다. Numeric value는 header의 explicit
registry가 소유하며 unknown value의 spelling은 `unknown`이다.

| Status | ASCII |
| --- | --- |
| `OK` | `ok` |
| `INVALID_ARGUMENT` | `invalid-argument` |
| `UNSUPPORTED_RUNTIME_ABI` | `unsupported-runtime-abi` |
| `UNSUPPORTED_HELPER_ABI` | `unsupported-helper-abi` |
| `INVALID_SIZE` | `invalid-size` |
| `RESERVED_NONZERO` | `reserved-nonzero` |
| `INVALID_DESCRIPTOR` | `invalid-descriptor` |
| `INVALID_STATE` | `invalid-state` |
| `DIGEST_MISMATCH` | `digest-mismatch` |
| `LIMIT_EXCEEDED` | `limit-exceeded` |
| `NOT_AUTHORIZED` | `not-authorized` |
| `NOT_PREPARED` | `not-prepared` |
| `ARENA_TOO_SMALL` | `arena-too-small` |
| `EMBEDDER_REJECTED` | `embedder-rejected` |
| `ALREADY_CONSUMED` | `already-consumed` |
| `INTERNAL_ERROR` | `internal-error` |

Outcome tag와 ASCII spelling은 `BOOT_ACTION`/`boot-action`,
`POLICY_ERROR`/`policy-error`, `VM_FAULT`/`vm-fault` 세 개다. Zero와 unknown tag는
terminal outcome이 아니며 `invalid-state`로 거부한다.

Fault code와 ASCII spelling은 다음과 같다. `NONE`은 receipt sentinel이며 terminal fault
outcome에 사용할 수 없다.

| Fault | ASCII | 의미 |
| --- | --- | --- |
| `INTERNAL` | `internal` | Runtime invariant failure |
| `INVALID_STATE` | `invalid-state` | Illegal lifecycle transition |
| `INSTRUCTION_BUDGET` | `instruction-budget` | Instruction counter exhaustion |
| `HELPER_BUDGET` | `helper-budget` | Helper-call counter exhaustion |
| `OPERATION_BUDGET` | `operation-budget` | External operation bound |
| `POLL_BUDGET` | `poll-budget` | Bounded poll exhaustion |
| `DEADLINE` | `deadline` | Monotonic deadline violation |
| `STACK_BOUNDS` | `stack-bounds` | Frame/stack range violation |
| `CALL_DEPTH` | `call-depth` | Direct-call depth violation |
| `LOOP_BOUND` | `loop-bound` | Verified loop count violation |
| `ARITHMETIC` | `arithmetic` | Checked arithmetic failure |
| `INVALID_VALUE` | `invalid-value` | Runtime type/value invariant |
| `HANDLE_VIOLATION` | `handle-violation` | Opaque ownership/provenance failure |
| `CAPABILITY` | `capability` | Capability gate failure |
| `MODE_PHASE` | `mode-phase` | Product mode/lifecycle phase failure |
| `HELPER_CONTRACT` | `helper-contract` | Helper ABI/result contract failure |
| `EMBEDDER` | `embedder` | Embedder callback failure |
| `TERMINAL_ACTION` | `terminal-action` | Terminal action closure failure |
| `RECOVERY` | `recovery` | Recovery notification invariant failure |

## Effective runtime limit

`RibosVmLimits`는 artifact resource closure, product/mode limit과 caller arena limit의
교집합이다. Prepare는 이 교집합을 줄일 수 있지만 artifact가 선언한 상한보다 넓힐 수
없다.

Exact caller-owned arena layout, generic/product cap 교집합과 typed value storage는
{doc}`ribos-runtime-storage-v1`이 소유한다. Runtime size query는 verifier가 재계산한
stack/depth와 product handle/output/trace cap을 함께 닫고 required byte가 product
arena cap을 넘으면 opcode dispatch 전에 거부한다.

Scalar와 direct control-flow의 incremental 실행 의미는
{doc}`ribos-scalar-interpreter-v1`이 소유하고 explicit direct-call frame과 verified
loop counter는 {doc}`ribos-bounded-calls-loops-v1`이 소유한다. 이 engine의
`RETURNED`는 entry 함수 반환이며 sealed `BootAction`이나 full-policy outcome이
아니다. Bounded aggregate 실행은 {doc}`ribos-bounded-aggregate-runtime-v1`이
소유한다. Generation handle, dynamic ownership과 bounded cleanup은
{doc}`ribos-generation-handles-v1`이 소유한다. Product helper dispatch와 recovery
notification이 닫히기 전에는 production execute entry로 사용하지 않는다.

| Field | 집행 지점 |
| --- | --- |
| `maximum_instructions` | instruction dispatch 전 |
| `maximum_helper_calls` | helper callback 전 |
| `maximum_stack_bytes` | frame push와 runtime layout |
| `maximum_arena_bytes` | runtime size query와 initialization |
| `maximum_input_bytes` | context와 helper input marshal |
| `maximum_output_bytes` | helper result, outcome와 receipt |
| `maximum_operations` | external operation 시작 전 |
| `maximum_polls` | bounded poll 전 |
| `maximum_execution_duration_ns` | 전체 execute monotonic deadline |
| `maximum_helper_duration_ns` | 한 synchronous callback deadline |
| `maximum_call_depth` | direct frame push 전 |
| `maximum_handles` | opaque generation handle table |
| `maximum_trace_records` | optional bounded diagnostic trace |

Instruction, helper, stack, arena, operation, duration과 call-depth 상한은 nonzero다. Arena는
stack보다 작을 수 없고 helper duration은 전체 execution duration보다 클 수 없다.
Poll, handle와 trace 상한은 해당 기능이 없는 product에서 zero일 수 있다.

## Context

`RibosVmContext`는 caller-owned immutable input view다.

- `bytes`는 native C structure가 아니라 artifact type table이 정의한 VM value
  encoding이다.
- `context_type_id`는 selected schema의 policy context type과 일치해야 한다.
- `selected_mode`와 `selected_phase`는 product가 정의한 `0..63` ID다.
- Context와 helper descriptor의 mode/phase mask는 `1ull << ID`로 대조한다.
- `generation`은 nonzero이며 한 execute snapshot을 식별한다.
- `digest`는 context byte의 SHA-256이고 zero digest는 유효하지 않다.
- VM은 execute가 반환할 때까지만 byte view를 borrow한다.

Context pointer, generation 또는 digest가 같다는 사실만으로 execution authorization이
생기지 않는다. PreparedProgram과 effective limit 검사가 먼저 성공해야 한다.

## Semantic schema와 execution contract

Product schema 1.1은 다음 의미의 유일한 권위다.

- source와 VM type
- helper argument와 result type
- capability
- borrow와 consume
- typestate transition parameter
- terminal boot-action flag

Helper execution contract 1.0은 다음 target 의미만 추가한다.

- pure, ephemeral, journaled, terminal effect
- synchronous execution mode
- 허용 product mode와 lifecycle phase
- input/output byte 상한
- operation과 poll 상한
- monotonic nanosecond deadline
- durability evidence
- runtime handle-table transition
- process-local callback binding

따라서 execution metadata를 추가해도 schema를 1.2로 올리지 않는다. Type, signature,
capability, ownership 또는 terminal 의미가 바뀌면 schema version과 digest가 바뀐다.
I/O bound, deadline, effect durability 또는 callback implementation이 바뀌면 helper
execution digest가 바뀐다.

Prepare는 stable helper ID, capability, terminal flag, transition parameter와 opaque
ownership이 두 contract에서 모순되지 않는지 검사한다. 모순은 더 좁은 contract를
선택하는 fallback이 아니라 terminal preparation failure다.

## Helper execution descriptor

Helper binding은 descriptor와 process-local callback으로 구성한다. Table은 stable ID
오름차순이며 최대 256개다. Duplicate ID, null callback과 unknown field는 거부한다.

Runtime v1 callback은 synchronous다. Callback은 다음을 지켜야 한다.

- `RibosVmHelperCall`은 callback 동안만 borrow한다.
- VM에 재진입하거나 call/argument/result pointer를 보존하지 않는다.
- Runtime이 제공한 typed accessor와 output capacity를 우회하지 않는다.
- `OK`, `POLICY_ERROR`, `CONTRACT_FAULT` 중 하나만 반환한다.
- Instruction fuel을 elapsed-time 제한으로 해석하지 않는다.
- Operation, poll과 deadline receipt를 반환 전에 닫는다.

Effect와 durability 조합은 다음과 같다.

| Effect | Durability |
| --- | --- |
| `PURE` | `NONE` |
| `EPHEMERAL` | `VOLATILE` |
| `JOURNALED` | `JOURNAL_RECEIPT` |
| `TERMINAL` | `SEALED_INTENT` |

Handle transition은 `NONE`, `CREATE`, `CONSUME`, `REPLACE`,
`TERMINAL_CONSUME`다. Consume 계열은 schema parameter index를 지정하고 create와 none은
invalid parameter sentinel을 쓴다. Runtime transition은 schema ownership을 넓힐 수
없다.

## Helper execution identity

Helper execution identity는 callback address와 native padding을 포함하지 않는다.
Canonical byte sequence는 다음 순서다.

```text
32-byte zero-padded ASCII domain "RIBOS-HELPER-EXECUTION-V1"
u16 contract major
u16 contract minor
u32 row count
row[stable ID order]
```

Row 하나는 다음 88 byte의 unsigned little-endian 값이다.

```text
u32 stable ID
u32 flags
u32 required capabilities
u32 effect
u32 execution mode
u32 durability
u32 handle transition
u32 transition parameter
u64 allowed mode mask
u64 allowed phase mask
u64 maximum input bytes
u64 maximum output bytes
u64 maximum operations
u64 maximum polls
u64 maximum duration ns
```

Digest는 canonical sequence의 SHA-256이다. `size`, reserved field, callback과
`embedder_context`는 encoding에서 제외한다. Prepare는 descriptor를 caller-owned
immutable storage에 복사해 callback pointer mutation과 원본 table mutation에서 실행
state를 분리한다.

Prepared binding identity는 다음 canonical sequence의 SHA-256이다.

```text
32-byte zero-padded ASCII domain "RIBOS-PREPARED-BINDING-V1"
32-byte artifact payload hash
32-byte product schema digest
32-byte helper execution digest
u16 runtime ABI major
u16 runtime ABI minor
effective-limit encoding
```

Effective-limit encoding은 `flags` `u32`, header 순서의 열 개 `u64` 상한,
`maximum_call_depth`, `maximum_handles`, `maximum_trace_records` 세 `u32`다. Reserved
field와 native padding은 제외한다. Artifact는 schema digest를 계속 봉인하고 helper
execution digest는 product authorization과 PreparedProgram이 결박한다.

## Query, prepare와 execute state machine

Runtime object는 다음 단방향 state를 따른다.

```text
EMPTY
  | query workspace/runtime size
  v
AUTHORIZED
  | structural open + authorization + Stage-1 + Stage-2
  | schema/execution digest binding + immutable copy
  v
PREPARED
  | context/embedder/effective-limit validation
  v
EXECUTING
  | exactly one terminal outcome
  v
ACTION | POLICY_ERROR | FAULT
  | consume or reset
  v
CONSUMED
```

Size query는 state를 만들지 않는다. Partial authorization, verifier failure, undersized
workspace와 digest mismatch는 `PREPARED`를 만들지 않는다. Production execute API는 raw
artifact byte를 받지 않고 opaque `RibosPreparedProgram`만 받는다. Executing object의
재진입, terminal outcome의 재실행과 consumed action의 재소비는 거부한다.

## Outcome과 ownership

`RibosVmOutcome`은 exactly one of three tag만 가진다.

### BootAction

`BootAction`은 terminal helper가 만든 sealed boot intent다.

- Actual OS jump, environment quiesce와 durable boot-attempt commit이 아니다.
- Payload는 VM arena에 속하며 outcome consume 또는 runtime reset까지 borrow한다.
- Generation과 receipt digest는 nonzero다.
- Product consumer만 action을 한 번 consume해 Ribon boot transaction으로 변환할 수
  있다.
- VM과 helper callback은 action 생성 뒤 다른 instruction이나 effect를 실행하지 않는다.

### PolicyError

`PolicyError`는 policy가 명시적으로 반환한 typed error다. Payload는 outcome 수명 동안
borrow하고 factory recovery callback을 자동 호출하지 않는다. Product가 recovery
정책으로 해석할 수 있지만 VM fault로 재분류하지 않는다.

### VmFault

`VmFault`는 catch할 수 없는 runtime failure다. Receipt는 pointer와 secret 없이 fault,
subject, instruction/helper 위치, consumed budget, 마지막 effect/durability, artifact
hash와 optional trace digest를 보존한다.

VM은 mutable handle과 callback authority를 revoke하고 receipt를 봉인한 뒤 factory
recovery callback을 최대 한 번 호출한다. Callback은 receipt를 callback 동안만 borrow하고
outcome을 바꾸거나 BootAction을 반환하거나 VM에 재진입할 수 없다. Callback 반환 뒤에도
최종 outcome은 같은 `VmFault`다. Durable external effect의 일반 rollback은 주장하지
않는다.

## 검증과 증거 한계

```sh
make check-ribos-runtime-contract
```

Gate는 runtime/helper version, size, reserved field, limit, descriptor ordering, callback,
context와 세 outcome tag의 fail-closed validation을 host C unit으로 검사한다. Public
header scan은 packed layout, native-width field, host/firmware/OS include와 Ribon service
type 유입을 거부한다.

이 성공은 ABI와 host compile/unit 증거다.
{doc}`ribos-prepared-program-v1`과 `make check-ribos-prepared-program`은 별도로
authorization, two-stage verification과 immutable binding 수명을 검증한다. 이 두
gate와 `make check-ribos-runtime-storage` 모두 opcode interpreter, helper 실행,
Ribon service adapter, QEMU와 hardware policy 실행을 증명하지 않는다.
