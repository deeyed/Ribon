---
doc_type: canonical
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/
  - language/ribos/frontend/include/ribos/frontend/compiler.h
  - language/ribos/schema/include/ribos/schema/schema.h
  - language/ribos/ir/include/ribos/ir/
  - language/ribos/artifact/include/ribos/artifact/
  - language/ribos/vm/include/ribos/vm/
  - include/Ribon/plugin/
  - src/core/
  - src/plugins/
tests:
  - ribos-grammar-generation
  - ribos-parser-conformance
  - ribos-typed-ast
  - ribos-type-negative
  - ribos-capability-negative
  - ribos-deterministic-semantic-dump
  - ribos-product-schema-identity
  - ribos-policy-ir-v1
  - make check-ribos-artifact
  - make check-ribos-verifier
  - ribon-docs
hardware:
  - none
supersedes:
  - untyped policy scripting model
---

# Ribos 언어 모델

Ribos는 Ribon의 mechanism을 bounded policy program으로 조합하는 정적 타입
Pre-OS language다. Ribos는 범용 application, kernel driver 또는 firmware native
driver를 작성하는 언어가 아니다.

## 이름과 제품 관계

Ribon과 Ribos는 다음 관계를 가진다.

```text
Ribon
  boot runtime library, plugin SDK, firmware product와 policy VM

Ribos
  Ribon policy VM의 typed source language
```

`Ribos`의 생물학적 모티프는 ribose가 RNA 구조를 지탱하는 backbone이라는 점이다.
Ribos program은 hardware mechanism을 구현하지 않고 이미 검증된 helper와 handle의
구성 순서 및 조건을 표현한다.

공식 언어 표기는 `Ribos`, source 확장자는 `.rbs`, compiler 이름은 `ribosc`다.
대문자 `OS`를 분리한 `RibOS` 표기는 운영체제로 오해될 수 있으므로 사용하지 않는다.

## 책임

Ribos program은 다음 결정을 표현할 수 있다.

- board fact와 product fact에 따른 typed profile 선택
- 검증된 device initialization helper의 호출 순서
- boot source, slot, recovery image와 verified object 선택
- bounded update 조건과 inactive-slot transaction 요청
- auxiliary core에 전달할 verified firmware 선택
- Device Tree base와 overlay의 검증된 조합 선택
- OS handoff의 typed field 또는 typed property 생성
- failure receipt에 따른 bounded recovery action
- production capability가 제한된 inspection shell operation

Ribos program은 다음 mechanism을 구현하지 않는다.

- raw MMIO register access
- clock, reset, pinctrl와 DRAM controller의 native register sequence
- raw flash offset write
- packet parser, TCP/IP stack와 NIC interrupt handler
- signature algorithm과 root-key store
- executable relocation과 architecture transfer assembly
- permanent page table, scheduler, process와 OS runtime driver
- firmware ABI의 handle database나 native event dispatcher

이 mechanism은 C backend, Architecture Backend, Environment, Driver, Image Format,
Security 또는 Firmware Personality plugin이 소유한다.

## Compiler와 runtime pipeline

Ribos의 권위 흐름은 다음과 같다.

```text
UTF-8 .rbs source
        |
        v
Pegen-generated host parser
        |
        +--> tracked C snapshot, digest staleness gate
        |
        v
lossless token/trivia와 bounded AST
        |
        v
name, type, mutation, effect, capability와 source bound 검사
        |
        +--> deterministic typed-AST/source-map dump
        |
        v
Ribos policy IR
        |
        +--> typed slot, explicit CFG, helper call-site와 product schema identity
        |
        v
canonical VM ABI 1.0/ISA 1.0 artifact
        |
        +--> little-endian section table, payload SHA-256와 signature envelope
        |
        v
Ribon structural reader와 compiler-independent Stage-1 verifier
        |
        v
Ribos VM
        |
        v
typed semantic helper
        |
        v
Ribon service/plugin mechanism
```

Source parser, AST와 source diagnostic은 host compiler의 권한이다. Boot product는
signed policy artifact를 검증하고 실행하는 데 `.rbs` source나 CPython runtime을
요구하지 않는다.

Language implementation은 하나의 monolithic runtime이 아니다.

```text
language/ribos/frontend  source, token/trivia, AST, type/effect와 IR lowering
language/ribos/schema    product-generated type/helper/handoff schema identity
language/ribos/ir        VM 독립 typed CFG와 structural validator
language/ribos/artifact  VM ABI/ISA와 canonical signed wire envelope
language/ribos/vm        Stage-1 bytecode verifier와 runtime ownership
```

Frontend private AST와 Pegen runtime은 VM dependency가 아니다. VM backend는 Policy IR
또는 그 검증된 serialized artifact만 소비한다.

Token model은 parser token과 formatter/debug source mapping을 위한 trivia를
분리한다. Token과 trivia는 immutable source span을 참조하고 stable byte range를
가진다. AST와 semantic dump는 process address가 아니라 numeric node/type ID만
노출한다.

Source-level checker는 bounded loop와 reachable call graph에서 helper-call upper
bound를 계산한다. Bytecode instruction upper bound는 Policy IR lowering 뒤에
계산한다. AST node count를 VM instruction budget으로 해석하지 않는다.

Policy IR resource analyzer는 parser나 typed AST를 사용하지 않고 reachable block,
bounded loop, terminal path, direct call graph, type/slot layout, frame/stack byte와
instruction/helper upper bound를 다시 계산한다. Compiler는 이 결과로 source의
`instruction_budget`과 `helper_budget`을 집행한다. Stage-1 artifact verifier는 type,
direct CFG, definite initialization과 frame/stack을 독립적으로 재검사한다. 후속
resource verifier와 VM은 exact instruction/helper upper bound를 다시 닫고 runtime
counter를 감소시켜야 한다.

Policy IR은 function-owned typed virtual slot, explicit basic block, direct branch/call,
phi-free explicit move, aggregate shape, source map과 helper call-site table을 가진다.
Expression과 argument는 source의 left-to-right 순서로 낮춘다. Integer arithmetic은
wraparound가 아니라 checked operator로 기록하며 runtime overflow는 catchable
exception이 아닌 policy fault다.

VM ABI 1.0은 Policy IR typed slot을 virtual register로 직접 사용한다. Register ID와
slot ID는 같고 최대 16,384개이며 별도 operand stack이나 physical-register allocator가
없다. Artifact는 fixed instruction row와 operand table, type/function/block/slot/helper
table, capability·resource budget와 schema digest를 explicit little-endian으로
직렬화한다. Packed host structure는 wire ABI가 아니다.

Artifact payload의 SHA-256는 byte identity를 제공한다. Optional Ed25519 envelope는
canonical signing message와 product key ID를 결합한다. Structural reader는
signature의 암호학적 유효성이나 bytecode semantics를 주장하지 않으며, product key
policy와 hostile-byte verifier가 실행 전에 각각 독립적으로 통과해야 한다.

Product type, member, helper signature/capability와 handoff field는 frontend C table의
고정 의미가 아니다. Product/plugin graph가 생성하는 versioned schema artifact가
compiler와 verifier의 공동 입력이다. Policy IR은 canonical schema bytes의 SHA-256을
봉인한다.

Pegen grammar는 source syntax의 machine-readable 정본이다. EBNF contract는 사람이
검토하는 규범 표현이고 Pegen generation 및 syntax corpus가 두 표현의 동등성을
검증한다. Generated C는 추적되는 operational snapshot이며 문법 정본이 아니다.
일반 build는 snapshot을 컴파일하고 digest를 검사하며 Pegen 실행은 explicit
regeneration operation으로 제한한다.

## 신뢰 계층

Ribos policy는 Ribon Core보다 낮은 신뢰 등급에 있다.

```text
hardware root와 immutable recovery
        >
Ribon Core, verifier와 semantic helper implementation
        >
signed Ribos policy artifact
        >
runtime input, board fact와 update manifest
```

Policy capability로 다음 권한을 얻을 수 없다.

- root key 교체
- signature check 우회
- rollback counter 감소
- verifier 비활성화
- Ribon Core arbitrary overwrite
- raw address jump
- arbitrary MMIO 또는 raw flash write
- production debug unlock

Policy가 verified object 중 하나를 선택할 수는 있지만 unverified byte sequence를
`VerifiedImage`로 생성할 수는 없다.

## Type와 value model

Ribos type은 object identity가 없는 값 또는 verifier가 생성한 opaque handle이다.

```text
scalar
  bool, u8, u16, u32, u64, i8, i16, i32, i64

value aggregate
  struct, enum, Array, List, FrozenMap, Dict, Option, Result

opaque handle
  Image, VerifiedImage, Slot, DeviceHandle, UpdateReceipt, BootAction
```

Pointer, raw address, untyped handle, `Any`, dynamic object와 user-visible heap reference는
없다. Opaque handle은 선언된 helper의 parameter와 return을 통해서만 생성되고 소비된다.

`let` binding은 immutable이고 `let mut` binding만 재대입할 수 있다. Struct field와
Array는 immutable이다. List와 Dict의 내용 변경에는 mutable binding과 mutating
operation의 effect가 모두 필요하다.

## Bounded model

다음 bound가 compile artifact와 product descriptor에 고정된다.

- source byte와 token count
- AST node와 parser nesting
- function, basic block과 call-graph depth
- bytecode instruction count
- VM instruction budget
- VM stack byte와 call depth
- helper call count와 helper별 deadline
- Array length와 List capacity
- FrozenMap cardinality와 Dict capacity
- loop의 최대 iteration
- handoff, log와 diagnostic output byte

한도를 초과하는 source는 compile failure이고, 한도를 위반하는 artifact는 verifier
failure다. 실행 input으로 인해 허용된 runtime capacity가 소진되면 typed error 또는
fail-closed policy fault가 발생한다. Capacity를 늘리기 위한 hidden heap fallback은
없다.

VM value는 host C ABI를 사용하지 않는다. Scalar, product named value, inline
aggregate와 tagged union은 Policy IR resource contract의 고정 layout을 사용한다.
List는 length와 inline element array, Dict와 FrozenMap은 length와 stable-order
key/value array를 사용한다. Dict lookup은 capacity-bounded linear search다.

## Effect와 capability

Pure expression은 immutable input을 읽고 값을 계산한다. Helper call은 descriptor에
선언된 effect와 capability를 가진다.

대표 effect class는 다음과 같다.

| Effect | 예 |
| --- | --- |
| `INSPECT` | reset reason, board revision, slot state 조회 |
| `DEVICE` | semantic device profile 적용, auxiliary core start |
| `STATE` | boot count, trial state와 journal transition |
| `NETWORK` | signed manifest와 verified object fetch |
| `FLASH` | inactive slot transaction |
| `HANDOFF` | typed handoff field와 reservation 작성 |
| `BOOT` | verified boot action 또는 recovery action 생성 |

`@policy` declaration은 허용 capability와 budget을 고정한다. Compiler와 verifier는
함수 call graph의 effect 합집합이 declaration을 넘지 않는지 검사한다. Runtime은 같은
capability를 product의 selected plugin graph와 다시 대조한다.

## Error와 fault

Ribos에는 exception, unwinding과 catch가 없다.

복구 가능한 domain failure는 다음 type으로 표현한다.

```text
Option[T]
Result[T, E]
```

`match`는 enum payload를 분해하고 postfix `?`는 `Result` 또는 `Option`의 failure를
호출자의 return type으로 전파한다.

Policy fault는 다음과 같은 contract 위반이다.

- verifier와 artifact 불일치
- instruction, stack 또는 helper budget 초과
- 불가능한 opcode와 invalid control transfer
- checked arithmetic trap을 처리하지 않은 실행
- helper implementation의 postcondition 위반

Policy fault는 catch할 수 없다. Ribon은 fault receipt를 봉인하고 product가 고정한
factory recovery path로 이동한다.

## OS와 board 독립성

Ribos Core Language는 Parus, Linux, FreeBSD, RPi5, QEMU와 특정 SoC 이름을 모른다.
OS protocol, board port와 firmware product가 generated type과 helper package를
제공한다.

```text
Ribos Core Language
    + Ribon generic helper package
    + selected board/port fact schema
    + selected OS protocol handoff schema
    + product capability manifest
```

Parus overseer policy는 Parus protocol companion package에 속한다. Generic boot slot,
verified image와 update transaction type은 Ribon generic package에 속한다.

## Shell 관계

Interactive shell은 Ribos의 별도 entry profile이다. Shell은 같은 lexical rule,
expression type, helper signature와 verifier를 재사용할 수 있다.

Production shell은 product가 허용한 inspection capability만 가진다. Source declaration,
struct/enum 정의, loop, update와 flash helper가 shell profile에 자동으로 열리지 않는다.
Shell grammar와 capability manifest는 full policy source보다 작은 subset일 수 있다.

## 비목표

Ribos는 다음을 목표로 하지 않는다.

- Python, Lua, Rust, Mojo 또는 WebAssembly source compatibility
- native application ABI
- self-hosting compiler
- JIT compilation
- garbage collection
- general-purpose package manager
- user-space application sandbox
- Ribon Core와 같은 신뢰 등급의 firmware implementation
