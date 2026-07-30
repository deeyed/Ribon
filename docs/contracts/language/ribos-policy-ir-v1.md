---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/ir/include/ribos/ir/ir.h
  - language/ribos/ir/include/ribos/ir/builder.h
  - language/ribos/ir/include/ribos/ir/analysis.h
  - language/ribos/ir/src/
  - language/ribos/schema/include/ribos/schema/schema.h
  - language/ribos/schema/src/schema.c
  - language/ribos/frontend/src/lower.c
tests:
  - make check-ribos-schema
  - make check-ribos-ir
  - make check-ribos-resources
  - make check
  - make docs
hardware:
  - none
supersedes:
  - implicit typed-AST to VM lowering
---

# Ribos Policy IR v1 계약

## 목적과 버전

Policy IR v1은 typed frontend와 bytecode emitter/artifact verifier 사이의 VM 독립
계약이다. Version은 `1.1`이다. Minor version은 기존 reader가 모르는 필드를
결정론적으로 무시할 수 있을 때만 증가한다. Opcode, type, control-flow 또는 fault
의미가 바뀌면 major version을 증가한다.

`RibosIrModule`은 host compiler의 bounded in-memory representation이다. Signed
artifact의 byte serialization은 별도 후속 계약이 소유한다.

## Module identity

Module은 다음 identity를 반드시 가진다.

```text
Policy IR format major/minor
selected product schema SHA-256
deterministically numbered tables
```

Schema digest가 zero이거나 selected verifier schema와 다르면 module을 거부한다.
Schema canonical encoding은 product ID, stable-ID-ordered type/member/helper/handoff
table을 포함한다. Pointer, padding, host endian과 link address는 포함하지 않는다.

Reference host schema identity는 다음 값이다.

```text
da48c96b07390ecbadb6eef06ab6cdfbd
07b9a6de1bb1aa8e876e19f24378f52
```

이 값은 generic product의 영구 schema라는 뜻이 아니라 reference corpus의 drift
detector다.

## ID와 ordering

Type, shape, constant, function, block, slot, instruction, source map과 helper call-site는
각 table에서 0부터 연속 증가하는 ID를 가진다. Invalid optional reference는
`UINT32_MAX`다.

동일 source bytes, compiler version과 product schema는 byte-for-byte 같은
deterministic text dump를 생성해야 한다. Hash table iteration, pointer order와
allocator address는 ordering 근거가 될 수 없다.

## Type와 aggregate

IR type record는 scalar width, collection element/key/value와 capacity, `Option`,
`Result`, named product type 및 user `struct`/`enum`을 구분한다.

Product-generated named type은 product schema type class에서 도출한 `abi_size`와
`abi_alignment`를 가진다. Policy IR v1.1에서 product enum은 `4/4`, opaque handle,
fact와 기타 value type은 `8/8` byte representation을 사용한다. User aggregate의
layout은 shape table에서 resource analyzer가 계산하므로 이 두 필드는 zero다.

Sum type tag는 다음과 같다.

| Type | Tag 0 | Tag 1 |
| --- | --- | --- |
| `Option[T]` | `Some(T)` | `None` |
| `Result[T, E]` | `Ok(T)` | `Err(E)` |

User enum tag는 source declaration 순서의 zero-based index다.

User aggregate는 AST pointer 대신 shape table을 가진다.

```text
struct field:
  owner type + field ordinal + value type + field name

enum variant:
  owner type + variant tag + variant name

enum payload:
  owner type + variant tag + payload ordinal + value type
```

`BUILD_STRUCT` operand는 field declaration 순서다. `BUILD_VARIANT` operand는 payload
declaration 순서다. Shape와 맞지 않는 operand count/type은 verifier failure다.

## Typed virtual slot

모든 slot은 정확히 하나의 owner function과 resolved type을 가진다. 다른 function의
slot을 operand로 참조할 수 없다.

Policy IR v1 lowering은 SSA phi를 생성하지 않는다. Branch merge result는 merge 전에
할당한 typed slot에 각 predecessor가 explicit `MOVE`를 기록한다. `let mut`과 loop
index도 typed slot과 explicit `MOVE`를 사용한다.

Block parameter field는 format에 예약되어 있으나 v1 frontend는 explicit-move
lowering을 사용하고 parameter count를 zero로 기록한다. Backend가 임의로 phi를
추론해 observable fault/evaluation order를 바꿀 수 없다.

## Control flow와 call

모든 function은 자기 block range 안의 entry block을 가진다. 모든 block은 정확히 한
terminator로 끝난다.

```text
JUMP    direct block target
BRANCH  bool operand + direct true/false block target
RETURN  function return type operand
TRAP    fail-closed internal/fault reason
```

Indirect branch, indirect call, function pointer와 arbitrary address target은 없다.
`CALL_DIRECT` target은 module function ID다. `CALL_HELPER` target은 selected product
schema의 nonzero helper stable ID다.

`for` back-edge는 별도 loop table row를 가져야 한다.

```text
function
header block
body entry block
exit block
latch block 또는 INVALID_ID
trip count
source map
```

Header는 body/exit로 직접 branch하고 그 instruction의 immediate는 trip count와
같아야 한다. Latch가 존재하면 header로 직접 jump한다. Loop table에 포함되지 않은
reachable CFG cycle은 resource closure failure다.

## Evaluation order

Expression evaluation은 source의 left-to-right 순서다.

- function/helper argument는 source argument 순서
- list element는 lexical 순서
- map entry는 lexical 순서, 각 entry는 key 뒤 value
- struct field argument는 declaration/source의 검증된 순서
- enum payload는 declaration/source 순서
- binary operand는 left 뒤 right

`and`와 `or`는 direct branch를 사용하는 short circuit다. 실행되지 않는 operand의
helper call, fault와 diagnostic effect는 발생하지 않는다.

If-expression merge는 각 선택 branch에서 result slot으로 `MOVE`한 뒤 merge block으로
직접 jump한다.

## Checked operator 의미

IR operator ID는 frontend `RibosOperator` 값과 독립이다. Lowering이 public
`RibosIrCheckedOperator`로 명시 변환한다.

정수 `ADD`, `SUBTRACT`, `MULTIPLY`, `NEGATIVE`와 overflow 가능한 left shift는
수학적 결과가 destination type 범위를 벗어나면 policy fault다. Wraparound는 없다.

다음도 policy fault다.

- zero divisor
- signed minimum을 `-1`로 divide 또는 remainder
- operand bit width 이상 shift
- signed type 범위를 벗어나는 shift 결과

Right shift, bitwise operation, equality, ordering, membership와 unary positive는
operand type이 허용하는 범위에서 total operation이다. Semantic type mismatch는
frontend 또는 verifier failure이며 runtime coercion은 없다.

Arithmetic fault는 Ribos exception이 아니다. Catch, unwind와 retry handler가 없으며
VM fault boundary가 factory recovery 경로를 선택한다.

## `Option`, `Result`와 propagation

`VARIANT_TAG`는 closed sum의 tag를 반환한다. `VARIANT_PAYLOAD`는 statically known tag와
payload ordinal을 가진다.

Postfix `?`는 다음 CFG로 낮춘다.

```text
value
  -> tag compare
  -> success: payload를 다음 expression slot으로 전달
  -> failure: 동일 error/None variant를 function return type으로 재구성하고 RETURN
```

Catchable exception이나 hidden call frame은 생성하지 않는다.

## Source map

AST-derived instruction과 slot은 stable source-map ID를 가진다. Source map은 AST node
ID, half-open byte range와 1-based line/column range를 보존한다. Raw source pointer는
보존하지 않는다.

Synthetic control-flow instruction도 가장 가까운 owner construct의 source map을
가져야 한다. Source map이 없거나 range가 역전되면 module을 거부한다.

## Helper call-site table

모든 `CALL_HELPER` instruction은 정확히 하나의 call-site row를 가진다.

```text
instruction ID
helper stable ID
required capability bits
resolved result type
argument count
source map ID
```

Call-site row는 instruction order로 증가한다. Row와 instruction의 helper ID, argument
count, result type 또는 source map이 다르면 module을 거부한다.

Compiler의 capability 검사는 verifier/runtime 검사를 대체하지 않는다. Verifier는
동일 digest의 product schema에서 helper signature와 capability를 다시 확인해야 한다.

## Capacity와 validation

Host v1 implementation limit은 다음과 같다.

```text
types             256
aggregate shapes  4,096
constants         4,096
constant bytes    1 MiB
functions         64
blocks            4,096
loops             1,024
slots             16,384
instructions      65,536
operands          131,072
source maps       65,536
helper call-sites 16,384
```

Capacity 초과는 partial authoritative module을 반환하지 않는다. Public compile API는
failure 때 caller module을 빈 v1 state로 reset한다.

Validator는 최소한 다음을 거부한다.

- invalid/noncontiguous table ID와 range
- zero/mismatched schema identity
- cross-function block 또는 slot reference
- 없는 direct branch/function target
- terminator가 없거나 terminator 뒤 instruction이 있는 block
- invalid constant/type/source-map reference
- aggregate shape와 다른 constructor
- invalid sum tag/payload arity
- helper instruction과 call-site table 불일치
- loop row와 header/body/exit/latch CFG 불일치

## 증거 경계

`make check-ribos-ir` 성공은 host typed-AST lowering, structural validation과
deterministic dump 증거다. `make check-ribos-resources`는 별도 resource-closure
계약에 따른 host CFG와 bound 분석 증거다. 다음을 증명하지 않는다.

- serialized/signed policy artifact
- adversarial artifact static verifier
- Ribos VM execution과 helper dispatch
- Ribon boot product integration
- QEMU, firmware 또는 physical hardware execution
