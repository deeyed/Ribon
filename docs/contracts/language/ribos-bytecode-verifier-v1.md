---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/vm/include/ribos/vm/verifier.h
  - language/ribos/vm/src/verifier.c
  - language/ribos/artifact/include/ribos/artifact/format.h
tests:
  - make check-ribos-verifier
  - make check-ribos-artifact
  - make check
hardware:
  - none
supersedes:
  - implicit hostile-byte verification
---

# Ribos bytecode two-stage verifier v1 계약

## 목적과 신뢰 경계

Verifier는 `.rba`를 생성한 compiler를 신뢰하지 않고 VM ABI 1.0/ISA 1.0 artifact의
구조와 Ribos 고유 의미를 두 단계로 다시 증명한다.

```text
untrusted .rba bytes
        |
        v
allocation-free structural reader
        |
        v
selected product schema identity
        |
        v
independent Stage-1 structural/type verifier
        |
        v
independent Stage-2 policy verifier
        |
        +--> semantic closure report, execution certificate 아님
        |
        v
signature, rollback와 runtime-counter gate
```

Verifier binary는 다음을 링크할 수 있다.

- artifact structural codec
- product schema descriptor와 canonical identity 함수
- `language/ribos/vm` verifier

다음은 링크할 수 없다.

- `.rbs` parser와 Pegen snapshot
- frontend AST와 semantic analyzer
- Policy IR validator와 resource analyzer
- artifact emitter

## Caller-owned workspace

`ribos_verifier_workspace_size_v1()`은 envelope, hash와 section range를 structural
reader로 확인한 뒤 필요한 scratch byte 수를 계산한다. 계산에는 다음이 포함된다.

- type layout state
- instruction ownership bitmap
- CFG reachability와 predecessor table
- per-function definite-assignment bit matrix
- direct-call graph, frame와 stack closure
- ownership must-availability와 terminal-action path state
- exact resource path와 helper별 bound closure

`ribos_verify_artifact_stage1_v1()`과 `ribos_verify_artifact_stage2_v1()`은 같은
artifact, selected schema와 caller-owned workspace를 받는다. Workspace가 작으면
semantic table을 읽기 전에 `workspace-too-small`로 거부한다. Verifier는 heap
allocation이나 host pointer의 wire 해석을 하지 않는다.

## Structural 재검사

Verifier entry는 structural reader를 반드시 통과한다.

- envelope와 payload header version
- canonical section order, row size, offset와 length
- checked offset/length arithmetic
- zero reserved byte와 padding
- payload SHA-256
- source-map section presence
- 알려진 opcode byte와 hard table limit

Opcode registry 위반은 structural reader에서 먼저 발견될 수 있다. 이 경우 Stage-1은
`structural-error`로 fail closed한다.

## Type와 constant table

Verifier는 ISA wire registry로 다음을 다시 계산한다.

- scalar, opaque handle와 integer width
- inline Array/List layout
- fixed-capacity sorted map layout
- Option/Result/enum tagged union layout
- declaration-order struct layout
- size, alignment, stride, payload offset와 capacity

Recursive by-value aggregate, 1 MiB 초과 value, alignment 8 초과와 산술 overflow는
거부한다. Encoded layout metadata는 재계산 값과 일치해야 한다.

Product named type은 selected schema에 존재해야 한다. Schema type class로 enum의
4-byte representation과 fact/value/opaque handle의 8-byte representation을
재확인한다.

Constant verifier는 ID, kind, byte range, zero flags/reserved와 FNV-1a stable hash를
constant bytes에서 다시 계산한다.

## Function, slot와 frame

Function block/slot range는 canonical contiguous partition이어야 한다. Entry function은
정확히 하나인 `POLICY` function과 일치한다.

Slot은 owner function, valid executable type, source-map reference와 frozen flag
registry를 만족해야 한다. Function slot ID 순서로 각 type alignment를 적용하여
frame offset을 다시 계산한다. Encoded slot offset, size, alignment와 function frame
byte가 다르면 거부한다.

```text
frame bytes
  = align(sum(slot-ID-order aligned type storage), maximum alignment)
```

단일 frame은 16 MiB를 넘을 수 없다.

## Instruction boundary와 direct target

각 block은 non-empty instruction chain 하나를 가진다.

- instruction ID와 owner block이 일치한다.
- `next`는 같은 block의 다음 row를 직접 가리킨다.
- instruction row는 둘 이상의 block에 속할 수 없다.
- 마지막 row만 terminal이며 `next=INVALID_ID`다.
- `JUMP`와 `BRANCH` target은 같은 function의 block이다.
- `CALL_DIRECT` target은 function table의 직접 ID다.
- operand slice와 constant/function/type index는 checked range 안에 있다.
- indirect call, indirect branch와 raw address target은 허용되지 않는다.

Block 끝의 유효 terminal은 `JUMP`, `BRANCH`, `RETURN`, `TRAP`이다. Fallthrough block,
중간 terminal과 terminal이 아닌 마지막 row는 거부한다.

## CFG와 definite initialization

Verifier는 function entry에서 direct edge를 따라 reachability를 계산한다. Preserved
unreachable block은 instruction shape와 operand type을 검사하지만 terminal mask,
definite initialization과 call graph에 기여하지 않는다.

Bounded loop row의 header branch, body/exit, trip count와 optional latch-to-header edge를
bytecode에서 다시 확인한다. 선언된 latch edge를 제거한 reachable CFG는 DAG여야
한다. 따라서 loop table에 없는 reachable cycle은 거부된다.

각 reachable block의 definite initialized set은 predecessor output의 교집합과 local
definition의 합집합이다.

```text
IN(entry) = empty
IN(block) = intersection(OUT(each reachable predecessor))
OUT(block) = IN(block) union local definitions
```

고정점 이후 instruction order로 operand read를 다시 순회한다. 해당 지점의 모든
incoming path에서 정의되지 않은 slot read는 `uninitialized-slot`이다. Parameter는
entry block에서 정확히 한 번 정의되어야 한다.

## Operand와 result type

Verifier는 opcode마다 operand count와 type relation을 검사한다.

- checked boolean/integer unary와 binary operator
- homogeneous Array/List와 map key/value
- struct field sequence와 Option/Result/enum variant payload
- declaration-order user struct field ordinal
- product fact member path와 selected schema result
- collection index와 length
- direct function parameter/return signature
- helper stable ID, selected schema parameter/result signature와 import identity
- boolean branch condition과 function return type

Helper wildcard parameter `*`는 schema가 의도적으로 모든 verified value type을
허용한 위치에서만 적용한다. Raw pointer type과 untyped operand는 없다.

## Direct-call storage closure

Reachable `CALL_DIRECT`만 call graph edge를 만든다. Recursive cycle은 거부한다.
Verifier는 각 function에 대해 다음을 다시 계산한다.

```text
maximum call depth
  = 1 + maximum reachable callee depth

maximum stack bytes
  = frame bytes + maximum reachable callee stack bytes
```

Encoded function call depth, function stack와 payload entry stack은 재계산 값과
일치해야 한다. Call stack은 64 MiB를 넘을 수 없다.

## Stage-2 schema와 helper ABI

Stage-2는 selected product schema format 1.1을 verifier 의미의 유일한 외부 권위로
사용한다. Canonical digest는 다음을 포함한다.

- policy context, action과 error entry ABI
- named type의 copy, affine 또는 linear ownership
- helper parameter와 result type
- parameter별 borrow 또는 consume mode
- helper capability
- typestate transition parameter
- terminal boot-action helper 표식

Entry policy는 schema context parameter 하나와
`Result[policy action, policy error]`를 사용해야 한다. Helper stable ID, argument와
result type은 Stage-1에서 확인하고 Stage-2는 ownership/transition/terminal 의미를
같은 schema row에서 해석한다.

Verifier는 성공 경로의 존재 자체를 요구하지 않는다. 존재하는 모든 `Ok` 경로는
정확히 하나의 terminal boot action을 반환해야 하고 `Err` 또는 trap 경로는 terminal
action을 반환할 수 없다. 따라서 모든 경로가 typed `PolicyError` 또는 trap으로
끝나는 정책도 유효하다.

## Reachable capability

각 function의 entry에서 도달 가능한 block과 direct callee만 capability closure에
기여한다. Stage-2는 helper capability와 callee closure의 OR를 재귀적으로 계산한다.

```text
reachable capability(function)
  = OR(direct reachable helper capability)
  | OR(reachable capability(direct reachable callee))
```

재계산 값은 function `required capabilities`와 정확히 같아야 하고 declared
capabilities의 subset이어야 한다. Entry 결과는 payload header의 required
capabilities와도 같아야 한다. Unreachable helper import는 import table의 구조에는
남을 수 있지만 실행 capability를 확장하지 않는다.

## Opaque provenance, typestate와 ownership

Product fact와 opaque handle은 symbol/integer constant, aggregate constructor 또는
raw bit pattern으로 만들 수 없다. Entry parameter, typed product member, helper
result, verified direct-call result와 ownership transfer operation만 provenance가 된다.

Typestate transition helper는 ownership-bearing parameter 하나를 `CONSUME`하고,
다른 ownership-bearing state 또는 `Unit` terminal state를 반환한다. Reference
schema의 예는 다음과 같다.

```text
Image --image.verify/consume--> VerifiedImage
UpdateReceipt --slot.mark_trial/consume--> Unit
VerifiedImage --boot.slot/consume--> BootAction
```

Aggregate가 affine/linear member를 포함하면 aggregate 전체가 같은 ownership
discipline을 상속한다. Verifier는 CFG must-availability dataflow를 실행한다.

- borrow와 consume은 모든 incoming path에서 available이어야 한다.
- consume 뒤 해당 value는 unavailable이다.
- move, aggregate/variant build, ownership payload extraction, by-value direct call과
  return은 ownership을 이전한다.
- helper는 schema의 parameter mode에 따라서만 consume할 수 있다.
- loop backedge와 merge를 포함해 unavailable value의 재사용 또는 이중 consume은
  `ownership-violation`이다.

Affine value는 사용하지 않고 종료할 수 있다. Linear value의 leak-free runtime
destruction은 VM 실행기의 별도 책임이며 verifier는 중복 consume과 위조를 우선
차단한다.

## Exact resource와 helper budget

Stage-2는 bounded loop와 direct-call DAG에서 compiler와 같은 수학적 정의를
독립 구현한다. Compiler resource analyzer는 verifier binary에 링크되지 않는다.

- instruction worst-case upper bound
- total helper-call upper bound
- function/helper stable-ID별 call upper bound
- maximum call depth와 stack closure

Loop body, early return과 trap은 별도 path outcome으로 계산하며 모든 offset/count와
bound 덧셈·곱셈은 overflow를 검사한다. 재계산 값은 function row, helper-bound
section과 entry header에 정확히 일치해야 하고 declared instruction/helper budget
이하여야 한다.

## Terminal action과 fail-closed fault

Terminal boot-action helper는 `BOOT` capability를 요구하고 schema의 policy action
type을 반환한다. 이 helper는 entry policy에서만 호출할 수 있다. 호출 뒤 다른 helper,
direct call, branch, jump 또는 trap을 실행할 수 없다.

Stage-2는 CFG에서 terminal action count를 `0`, `1`, `2 이상`으로 포화 전파한다.

- `Ok(policy action)` 성공 return은 정확히 한 terminal action을 가져야 한다.
- `Err(policy error)` return과 `TRAP` fault path는 terminal action이 없어야 한다.
- loop 또는 merge가 0/1/2 이상의 모호한 count를 만들면 거부한다.
- 성공 return이 하나도 없는 policy도 거부한다.

따라서 policy fault가 boot action 이후 정상 error처럼 되돌아가거나 두 개의 boot
action을 발행할 수 없다. VM은 TRAP을 catchable language exception으로 노출하지
않고 product factory recovery로 전달해야 한다.

## 결과와 후속 gate

성공 report는 검증한 table 수와 entry function의 재계산 frame, stack, call depth를
가진다. Report는 serialized certificate가 아니며 artifact bytes 또는 schema
identity와 분리해 cache할 수 없다.

Stage-2 성공은 좁은 의미의 Ribos compiler/verifier semantic closure다. 다음을
주장할 수는 없다.

- Ed25519 signature와 product key authority
- rollback counter와 installation policy
- runtime instruction/helper/loop counter 집행
- semantic helper implementation 안전성
- VM dispatch
- Ribon boot product, QEMU 또는 physical hardware policy 실행
