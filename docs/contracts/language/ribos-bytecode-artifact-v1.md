---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/artifact/include/ribos/artifact/
  - language/ribos/artifact/src/
  - language/ribos/host/include/ribos/host/artifact_emitter.h
  - language/ribos/host/src/artifact_emitter.c
  - language/ribos/ir/include/ribos/ir/
tests:
  - make check-ribos-artifact
  - make check-ribos-verifier
  - make check-ribos-ir
  - make check-ribos-resources
  - make check
  - make docs
hardware:
  - none
supersedes:
  - implicit Policy IR bytecode serialization
---

# Ribos bytecode ISA와 artifact v1 계약

## 목적과 적용 범위

이 계약은 validated Policy IR v1.1을 Ribon product가 설치·검증·실행할 수 있는
`.rba` artifact로 표현하는 wire format을 고정한다.

```text
128-byte envelope
  + executable payload
  + key identifier
  + signature
```

Artifact envelope version, VM ABI와 bytecode ISA version은 서로 독립적으로
`1.0`이다. Reader는 지원하지 않는 major/minor를 fail closed한다. 전체 artifact
크기는 32 MiB 이하이다.

Artifact는 packed C structure의 memory image가 아니다. 모든 multi-byte integer는
unsigned little-endian이고 byte reader/writer로 field별 직렬화한다. Host pointer,
native `size_t`, compiler padding, bit-field와 host endian은 wire에 나타나지 않는다.

## 수치와 ID 규칙

- Optional ID의 invalid 값은 `UINT32_MAX`다.
- Table ID는 0부터 연속 증가하며 row 순서와 일치한다.
- Offset은 자신을 포함한 payload 또는 artifact 시작 기준의 unsigned byte offset이다.
- 모든 reserved field와 alignment padding은 zero다.
- `offset + length`, `count * row_size`, alignment round-up과 host `size_t` 변환은
  overflow 검사를 통과해야 한다.
- 알려지지 않은 flag, opcode, section, nonzero reserved field, 겹치거나 비정규적인
  range는 거부한다.

## Signature envelope

Envelope는 정확히 128 byte이며 field는 다음과 같다.

| Offset | Size | Field | v1 value 또는 의미 |
| ---: | ---: | --- | --- |
| 0 | 8 | magic | `RIBOSA1\0` |
| 8 | 2 | major | `1` |
| 10 | 2 | minor | `0` |
| 12 | 4 | header bytes | `128` |
| 16 | 4 | flags | bit 0 `SIGNED` |
| 20 | 2 | hash algorithm | `1` = SHA-256 |
| 22 | 2 | signature algorithm | `0` = none, `1` = Ed25519 |
| 24 | 8 | payload offset | `128` |
| 32 | 8 | payload length | executable payload byte count |
| 40 | 8 | key ID offset | payload 직후 |
| 48 | 4 | key ID length | 0 또는 1..64 |
| 52 | 4 | signature length | 0 또는 64 |
| 56 | 8 | signature offset | key ID 직후 |
| 64 | 8 | total length | signature 직후이자 file size |
| 72 | 32 | artifact hash | payload의 SHA-256 |
| 104 | 24 | reserved | zero |

Unsigned artifact는 `flags=0`, algorithm `none`, key ID와 signature length가 모두
zero여야 한다. Signed artifact는 `SIGNED`, Ed25519, non-empty key ID와 64-byte
signature를 함께 가져야 한다. 부분적으로 signed인 조합은 없다.

Envelope range는 canonical하게 붙어 있어야 한다.

```text
payload offset = 128
key ID offset = payload offset + payload length
signature offset = key ID offset + key ID length
total length = signature offset + signature length = file length
```

Artifact hash는 signature envelope, key ID와 signature bytes를 포함하지 않고
executable payload 전체만 포함한다.

### Ed25519 signing message

Signer와 verifier는 다음 112-byte message에 Ed25519를 적용한다.

| Offset | Size | Value |
| ---: | ---: | --- |
| 0 | 32 | zero-padded ASCII domain `RIBOS-ARTIFACT-SIGNATURE-V1` |
| 32 | 2 | envelope major, little-endian |
| 34 | 2 | envelope minor, little-endian |
| 36 | 2 | signature algorithm, little-endian |
| 38 | 2 | zero |
| 40 | 8 | payload length, little-endian |
| 48 | 32 | payload SHA-256 |
| 80 | 32 | `SHA-256(key ID bytes)` |

Key ID는 key 자체가 아니며 product trust store의 immutable key 선택자다. Production
loader는 product policy에 따라 key ID를 trust root에 resolve하고 signature를
암호학적으로 검증해야 한다. Core codec의 hash와 envelope-shape 검사 성공은
signature 인증 성공을 의미하지 않는다.

## Executable payload header

Payload header는 정확히 160 byte다.

| Offset | Size | Field | v1 value 또는 의미 |
| ---: | ---: | --- | --- |
| 0 | 8 | magic | `RIBBC01\0` |
| 8 | 2 | VM ABI major | `1` |
| 10 | 2 | VM ABI minor | `0` |
| 12 | 2 | ISA major | `1` |
| 14 | 2 | ISA minor | `0` |
| 16 | 4 | header bytes | `160` |
| 20 | 4 | payload flags | bit 0 `HAS_SOURCE_MAP` |
| 24 | 4 | section count | 12 또는 13 |
| 28 | 4 | entry function | 유일한 policy function ID |
| 32 | 4 | virtual register count | global slot count |
| 36 | 4 | slot count | global slot count |
| 40 | 4 | declared capability bitmap | entry policy가 허용한 capability |
| 44 | 4 | required capability bitmap | reachable helper가 요구한 capability |
| 48 | 8 | instruction budget | policy 선언 상한 |
| 56 | 8 | instruction upper bound | resource closure 결과 |
| 64 | 8 | helper budget | policy 선언 상한 |
| 72 | 8 | helper upper bound | resource closure 결과 |
| 80 | 8 | maximum stack bytes | maximum direct-call path |
| 88 | 4 | maximum call depth | entry를 포함한 depth |
| 92 | 4 | reserved | zero |
| 96 | 32 | schema digest | canonical product schema SHA-256 |
| 128 | 8 | directory offset | `160` |
| 136 | 8 | directory length | `section count * 32` |
| 144 | 8 | payload length | envelope의 payload length와 동일 |
| 152 | 8 | reserved | zero |

`required capabilities`는 `declared capabilities`의 subset이어야 한다. Instruction과
helper upper bound는 각각의 declared budget 이하이어야 한다. Schema digest는
zero일 수 없다.

### Register와 slot 모델

ISA v1의 register는 machine register나 별도 operand stack이 아니라 Policy IR의
function-owned typed virtual slot이다.

```text
virtual register ID == slot ID
virtual register count == slot count
maximum count == 16,384
```

각 slot의 실제 저장 위치는 slot table의 function, frame offset, byte size와 alignment가
정한다. v1에는 독립 register allocator, register window, hidden evaluation stack과
implicit operand가 없다. 이후 physical-register ISA를 도입하려면 ISA version을
변경해야 한다.

## Section directory와 canonical layout

Directory descriptor 하나는 32 byte다.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | section kind |
| 2 | 2 | flags, v1은 zero |
| 4 | 4 | row size |
| 8 | 8 | payload-relative data offset |
| 16 | 8 | data length |
| 24 | 4 | row count |
| 28 | 4 | reserved, zero |

Section은 kind 순서로 정확히 한 번 나타난다. 각 data offset은 이전 section 끝을
8-byte 정렬한 위치이며 사이 padding은 zero다. 마지막 section 끝은 payload length와
같아야 한다. Row table은 `length == count * row_size`를 만족한다. Constant byte
section만 row size가 1이고 count가 byte count다.

| Kind | Section | Row size |
| ---: | --- | ---: |
| 1 | types | 128 |
| 2 | shapes | 32 |
| 3 | constants | 32 |
| 4 | constant bytes | 1 |
| 5 | functions | 104 |
| 6 | blocks | 32 |
| 7 | loops | 32 |
| 8 | slots | 32 |
| 9 | instructions | 48 |
| 10 | operands | 4 |
| 11 | helper imports | 16 |
| 12 | helper bounds | 16 |
| 13 | source maps | 40 |

Kind 1..12는 항상 존재한다. `HAS_SOURCE_MAP`가 set이면 kind 13이 마지막에 존재하고,
unset이면 kind 13과 모든 source-map reference가 생략된다.

## Type, value와 control-flow table

### Type row

Type row는 `id`, kind와 bit width, first/second referenced type, collection bound,
shape start/count, declared ABI size/alignment, computed storage kind/byte size/element
stride/payload offset/capacity, name length와 64-byte name storage를 순서대로 가진다.
Name length는 최대 63이고 사용하지 않은 name/reserved bytes는 zero다.

Type와 storage 의미는 Policy IR v1.1 resource-closure 계약을 그대로 사용한다.
List는 inline bounded array, Dict/FrozenMap은 stable-order fixed-capacity entry array다.
String-literal type의 `bound`는 literal UTF-8 byte length이며 zero-length literal이면
zero일 수 있다. Runtime value는 8-byte constant token이고 type `bound`를 inline
payload capacity로 해석하지 않는다.

### Shape row

Shape row는 다음 8개의 `u32` field로 고정한다.

```text
id, kind, owner type, variant tag, ordinal, value type, reserved, reserved
```

### Constant row와 bytes

Constant row는 다음 순서다.

```text
u32 id
u16 kind
u16 flags = 0
u32 byte offset
u32 byte length
u64 stable hash
u64 reserved = 0
```

Offset과 length는 constant-bytes section 내부 range다. Constant의 byte encoding과
stable hash 의미는 Policy IR 계약이 소유한다.

### Function row

Function row는 다음 순서로 고정한다.

```text
u32 id, flags, return type, entry block
u32 first block, block count, first slot, slot count
u32 parameter start, parameter count
u32 declared capabilities, required capabilities
u64 declared instruction budget, instruction upper bound
u64 declared helper budget, helper upper bound
u64 maximum stack bytes
u32 frame bytes, maximum call depth, terminal mask, reserved
```

Module은 정확히 하나의 `POLICY` function을 entry로 가진다. Direct-call graph와
reachable resource closure가 각 function row에서 재검산 가능해야 한다.

### Block와 loop row

Block row는 8개의 `u32`다.

```text
id, function ID, first instruction, last instruction,
instruction count, parameter start, parameter count, flags
```

Loop row도 8개의 `u32`다.

```text
id, function ID, header block, body block,
exit block, latch block, trip count, source-map ID
```

Loop table에 없는 reachable cycle은 유효한 bytecode가 아니다.

### Slot row

Slot row는 8개의 `u32`다.

```text
id, function ID, type ID, frame offset,
byte size, alignment, source-map ID, flags
```

Slot range는 owner function frame 안에 있어야 하며 정렬·겹침·type storage를
독립 verifier가 재계산한다. Function row의 frame bytes는 bytecode frame alignment
8의 배수이며 maximum stack bytes는 reachable frame들의 aligned 크기를 합한 값이다.

## Bytecode instruction encoding

Instruction은 variable-length C union이 아니라 고정 48-byte row와 별도 `u32`
operand table로 표현한다.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | opcode |
| 1 | 1 | flags, v1은 zero |
| 2 | 2 | operand count |
| 4 | 4 | instruction ID |
| 8 | 4 | owner block ID |
| 12 | 4 | result slot ID 또는 invalid |
| 16 | 4 | operand-table start |
| 20 | 4 | direct target function/block/helper ID |
| 24 | 4 | alternate block ID, struct field ordinal 또는 variant payload ordinal |
| 28 | 4 | source-map ID 또는 invalid |
| 32 | 4 | next instruction in block 또는 invalid |
| 36 | 4 | reserved, zero |
| 40 | 8 | opcode-specific immediate |

Operand table의 각 row는 slot ID 하나다. `operand start + operand count` range는
overflow 없이 operand table 안에 있어야 한다. Direct branch/call target 외의 address,
pointer와 indirect dispatch는 표현할 수 없다.

`MEMBER`가 user struct를 읽으면 `alternate`는 declaration-order field ordinal이고
`target` constant는 source/debug spelling이다. Product fact path는 selected schema가
resolve하므로 `alternate=INVALID_ID`다. VM은 user struct field를 source string이나
hash로 dispatch하지 않고 verifier가 type table과 대조한 ordinal로 접근한다.

Opcode registry는 다음과 같다.

| Byte | Opcode | Byte | Opcode |
| ---: | --- | ---: | --- |
| `0x01` | `PARAMETER` | `0x0d` | `BUILD_VARIANT` |
| `0x02` | `CONST_UNIT` | `0x0e` | `MEMBER` |
| `0x03` | `CONST_BOOL` | `0x0f` | `INDEX` |
| `0x04` | `CONST_INTEGER` | `0x10` | `COLLECTION_LENGTH` |
| `0x05` | `CONST_STRING` | `0x11` | `VARIANT_TAG` |
| `0x06` | `CONST_SYMBOL` | `0x12` | `VARIANT_PAYLOAD` |
| `0x07` | `MOVE` | `0x13` | `CALL_DIRECT` |
| `0x08` | `CHECKED_UNARY` | `0x14` | `CALL_HELPER` |
| `0x09` | `CHECKED_BINARY` | `0x15` | `JUMP` |
| `0x0a` | `BUILD_LIST` | `0x16` | `BRANCH` |
| `0x0b` | `BUILD_MAP` | `0x17` | `RETURN` |
| `0x0c` | `BUILD_STRUCT` | `0x18` | `TRAP` |

Opcode byte는 Policy IR enum의 numeric value와 독립적이다. Arithmetic, evaluation
order, aggregate와 terminal semantics는 Policy IR v1.1 계약을 따른다.

## Helper import와 capability

Helper import row는 다음 네 개의 `u32`다.

```text
stable helper ID, required capability bitmap, call-site count, reserved
```

Import는 stable helper ID 오름차순으로 unique하다. Maximum helper import 수는 256이다.
Compiler의 native pointer나 product table index는 저장하지 않는다.

Helper bound row는 다음과 같다.

```text
u32 function ID
u32 stable helper ID
u64 worst-path call upper bound
```

Verifier는 selected product schema에서 helper ID, signature, capability,
parameter borrow/consume mode, typestate와 terminal-action flag를 resolve하고,
instruction call-site count와 helper-bound/resource closure를 독립적으로 검사해야
한다. Schema format 1.1 canonical digest는 이 의미를 모두 봉인한다.

## Optional source map

Source-map row는 다음과 같다.

```text
u32 id
u32 AST node ID
u64 start byte
u64 end byte
u32 start line
u32 start column
u32 end line
u32 end column
```

Byte range는 half-open이고 line/column은 1-based다. Source bytes, file path와 host
pointer는 artifact에 포함하지 않는다. Source map을 제거해도 executable payload의
control-flow와 resource 의미는 변하지 않는다.

## Reader와 verifier의 신뢰 경계

Allocation-free v1 reader는 다음만 보장한다.

- envelope, version, canonical range와 zero reserved/padding
- payload SHA-256
- payload header, directory 순서와 section bounds
- 고정 row size, register/slot·helper-import hard limit
- capability/budget header의 기본 subset·상한 관계
- 알려진 opcode byte와 source-map presence 일관성

독립 artifact verifier는 compiler를 신뢰하지 않고 다음을 추가로 증명해야 한다.

- 모든 table ID/reference와 operand range
- type·slot·frame layout
- block/instruction ownership과 direct CFG
- reachable terminal closure와 bounded loop/call graph
- opcode operand/result type
- helper schema/signature/capability
- instruction, stack, call-depth와 helper upper bound
- entry function의 terminal action

`language/ribos/vm` Stage-1은 table ID/reference, type/slot/frame, instruction ownership,
direct CFG/call, reachable terminal, definite initialization, opcode type와 stack/call
depth를 구현한다. Stage-2는 exact instruction/helper worst-case upper bound,
helper-specific bound, reachable capability, opaque provenance, ownership/typestate와
terminal/fault closure를 구현한다. Runtime counter는 VM gate이며 Stage-2 report도
signature와 rollback을 포함한 실행 certificate가 아니다.

Production verifier는 그 뒤 signature와 rollback/product policy를 확인한다. Structural
reader 성공, host emitter 성공 또는 valid signature 중 어느 하나만으로 artifact를
실행할 수 없다.
