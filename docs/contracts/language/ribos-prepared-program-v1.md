---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/vm/include/ribos/vm/prepared.h
  - language/ribos/vm/src/prepared.c
  - language/ribos/vm/src/prepared_internal.h
  - language/ribos/artifact/src/sha256.c
tests:
  - make check-ribos-prepared-program
  - make check-ribos-artifact
  - make check-ribos-verifier
  - make check-ribos-host-boundary
  - make check
  - make docs
hardware:
  - none
supersedes:
  - implicit artifact-to-execute eligibility
---

# Ribos AuthorizedArtifact와 PreparedProgram v1 계약

## 목적과 보안 경계

이 계약은 product authorization, independent verifier와 runtime execution 사이의
time-of-check/time-of-use 경계를 닫는다. Generic VM은 raw `.rba` byte를 실행 가능한
상태로 해석하지 않는다.

```text
untrusted artifact bytes
        |
        v
structural open + exact-byte copy
        |
        v
product signature/key/rollback authority
        |
        v
opaque AuthorizedArtifact
        |
        v
selected schema + Stage-1 + Stage-2
        |
        v
helper execution binding + effective limits
        |
        v
opaque PreparedProgram
```

Production execute API는 `RibosPreparedProgram`만 받을 수 있다. Raw byte, structural
`RibosArtifactView`, verifier report 또는 authorization receipt 하나만으로 dispatch할
수 없다.

이 계약은 VM runtime ABI 1.0, helper execution contract 1.0, product schema 1.1,
artifact envelope/VM ABI/ISA 1.0을 사용한다. 각 version은 독립적으로 협상한다.

## 권위 분리

Generic VM이 소유하는 판단은 다음과 같다.

- artifact envelope, canonical range와 payload hash
- caller-owned immutable copy
- authorization receipt와 copied artifact의 일치
- Stage-1과 Stage-2 verifier 호출
- selected schema와 artifact schema digest의 일치
- selected helper execution table의 canonical digest
- schema helper 의미와 execution descriptor의 일치
- artifact resource closure와 effective limit의 일치
- PreparedProgram binding identity와 후속 mutation 검출

Product authority가 소유하는 판단은 다음과 같다.

- key ID를 trust root에 resolve하는 방법
- signature verification 구현
- manifest sequence와 monotonic rollback floor
- development/production key와 lifecycle policy
- 허용 helper execution identity
- authority generation과 policy identity

`RibosArtifactAuthorizeFn`은 후자의 판단을 수행하는 process-local callback이다. Generic
VM은 production key store, TPM, fuse 또는 rollback storage를 직접 구현하지 않는다.
Callback이 `OK`를 반환해도 receipt field와 artifact hash가 일치하지 않으면
authorization은 실패한다.

## Authorization request와 receipt

Request는 callback 동안만 유효하며 exact copied artifact byte, envelope signed flag,
signature algorithm, key ID, signature, payload artifact hash와 schema digest를 가진다.

Receipt는 다음 fixed-width 값을 봉인한다.

- authorization ABI 1.0과 `GRANTED` decision
- nonzero authority generation
- manifest sequence와 rollback floor
- artifact payload hash와 product schema digest
- helper execution digest
- optional key identity digest
- nonzero product policy identity digest

`manifest_sequence < rollback_floor`는 거부한다. Signed envelope를 승인한 receipt는
nonzero key identity digest를 가져야 한다. Unsigned development artifact를 허용할지
여부는 product callback 정책이며 generic VM의 암묵적 bypass가 아니다.

Receipt identity는 native structure bytes가 아니라 다음 canonical little-endian
sequence의 SHA-256이다.

```text
32-byte domain "RIBOS-AUTHORIZATION-RECEIPT-V1"
u16 authorization major
u16 authorization minor
u32 flags
u32 decision
u64 authority generation
u64 manifest sequence
u64 rollback floor
32-byte artifact hash
32-byte schema digest
32-byte helper execution digest
32-byte key identity digest
32-byte policy identity digest
```

Size, pointer, reserved field, compiler padding과 native endian은 identity에 포함하지
않는다.

## AuthorizedArtifact lifetime

`ribos_authorized_artifact_workspace_size_v1()`과 alignment query는 caller-owned
workspace requirement를 계산한다. Authorization은 다음 순서를 지킨다.

1. Raw artifact를 caller workspace로 복사한다.
2. Copied range를 structural reader로 연다.
3. Product callback에 copied immutable request를 전달한다.
4. Callback 반환 뒤 copied range를 다시 structural open한다.
5. Request, receipt와 reopened view의 artifact/schema digest를 비교한다.
6. Artifact 전체 byte range의 SHA-256과 canonical receipt identity를 저장한다.
7. 모든 검사가 끝난 뒤에만 opaque object를 `AUTHORIZED`로 표시한다.

Payload hash만으로 signature byte mutation을 검출할 수 없으므로 내부 exact-byte
digest는 envelope, key ID와 signature를 포함한 전체 copied range에 적용한다.
Validation은 structural open, exact-byte digest와 receipt identity를 다시 계산한다.
Raw source byte는 함수 반환 뒤 borrow하지 않는다.

## PreparedProgram lifetime

Prepared workspace는 다음을 모두 caller-owned storage에 둔다.

- opaque PreparedProgram state
- exact artifact byte copy
- stable-ID 순서의 helper descriptor와 callback binding copy
- Stage-1/Stage-2 verifier scratch
- authorization receipt와 receipt identity
- verifier report와 effective runtime limits
- artifact, schema, helper와 binding digest

Selected `RibosProductSchema` pointer는 prepare 호출 동안만 borrow한다. Stage-2 뒤
canonical identity를 다시 계산하고 initial identity와 비교한 뒤 pointer를 보존하지
않는다. Runtime에 필요한 helper callback table은 복사하며 source binding pointer는
보존하지 않는다.

Prepare는 다음 순서를 지킨다.

1. AuthorizedArtifact의 exact-byte와 receipt seal을 재검사한다.
2. Selected schema identity를 artifact와 receipt에 대조한다.
3. Artifact를 PreparedProgram workspace로 다시 복사하고 exact-byte digest를 대조한다.
4. Copied artifact에서 Stage-1 verifier를 실행한다.
5. 같은 copied artifact에서 Stage-2 verifier를 실행한다.
6. Schema identity가 verifier 실행 전후 동일한지 확인한다.
7. Helper execution identity를 canonical field에서 다시 계산한다.
8. Helper digest를 contract 선언, receipt와 대조한다.
9. Schema helper와 execution descriptor 의미를 대조한다.
10. Helper descriptor와 callback을 PreparedProgram workspace로 복사한다.
11. Copied helper table identity를 다시 계산한다.
12. Effective limits를 verified resource closure에 대조한다.
13. Prepared binding identity를 계산하고 마지막에만 `PREPARED`로 표시한다.

Partial initialization, undersized workspace, authorization mismatch, verifier failure,
schema/helper mismatch와 limit mismatch는 PreparedProgram을 만들지 않는다.

## Schema와 helper execution consistency

모든 selected helper binding은 같은 stable ID의 schema helper를 가져야 한다.
Capabilities는 정확히 같아야 하며 terminal flag와 runtime terminal effect가 서로
일치해야 한다.

Runtime handle transition은 schema ownership을 넓힐 수 없다.

- non-typestate copy result: `NONE`
- non-typestate affine/linear result: `CREATE`
- typestate to `Unit`: `CONSUME`
- typestate to ownership-bearing result: `REPLACE`
- terminal typestate: `TERMINAL_CONSUME`
- terminal without consumed handle: `NONE`

Consume 계열 transition parameter는 schema parameter index와 같아야 한다. Artifact
helper-import table의 모든 stable ID와 capability에는 selected execution binding이
반드시 존재해야 한다. Execution table은 사용하지 않는 product helper를 더 포함할 수
있지만 각 row는 schema와 일치해야 한다.

Helper execution digest는 {doc}`ribos-vm-runtime-v1`의
`RIBOS-HELPER-EXECUTION-V1` canonical encoding을 사용한다. Callback address는 digest에
포함하지 않지만 copied binding에는 보존한다. 따라서 prepare 뒤 원본 callback table
변경은 PreparedProgram에 전파되지 않는다.

## Effective limit closure

`RibosVmLimits` 자체 validation 뒤 다음 관계를 강제한다.

```text
verified instruction upper
    <= effective maximum instructions
    <= artifact declared instruction budget

verified helper upper
    <= effective maximum helper calls
    <= artifact declared helper budget

verified stack bytes <= effective maximum stack bytes
verified call depth <= effective maximum call depth
```

나머지 arena, I/O, operation, poll과 duration limit은 runtime ABI validator가 검사하며
R09 이후 runtime size/execute gate가 실제로 집행한다.

Prepared binding identity는 runtime v1 계약의 canonical sequence를 사용한다.

```text
domain "RIBOS-PREPARED-BINDING-V1"
artifact payload hash
schema digest
helper execution digest
runtime ABI 1.0
effective-limit encoding
```

## Mutation과 fail-closed 규칙

- Raw source artifact mutation은 AuthorizedArtifact에 전파되지 않는다.
- AuthorizedArtifact copied byte mutation은 validation 또는 prepare에서 거부한다.
- Prepare 전 selected schema identity mutation은 digest mismatch다.
- Prepare 전 helper descriptor mutation은 declared/authorized digest mismatch다.
- Prepare 뒤 original artifact, schema와 helper table mutation은 copied state에
  전파되지 않는다.
- PreparedProgram의 copied artifact 또는 helper descriptor mutation은 validation에서
  거부한다.
- Helper callback은 PreparedProgram, artifact byte 또는 schema pointer를 받지 않는다.

Caller가 opaque workspace 전체를 임의로 다시 쓰는 memory-corruption 공격에 대해
software seal이 독립 trust root라고 주장하지 않는다. 이 계약은 authorized inputs와
execution state 사이 alias와 TOCTOU를 제거하고 accidental/cross-component mutation을
fail closed한다. Platform memory protection은 product integration 책임이다.

## API와 build graph hard gate

Public API는 다음 opaque type만 execution eligibility를 표현한다.

```c
typedef struct RibosAuthorizedArtifact RibosAuthorizedArtifact;
typedef struct RibosPreparedProgram RibosPreparedProgram;
```

`make check-ribos-prepared-program`은 production header/source graph에서 raw execute
signature와 `execute_bytes`, `execute_artifact`, `dispatch_bytes`,
`dispatch_artifact` 계열 symbol을 거부한다. Unsafe fixture loader가 필요하면 host test
binary 안에만 둔다.

## 검증과 증거 한계

```sh
make check-ribos-prepared-program
make check-ribos-artifact
make check-ribos-verifier
make check-ribos-host-boundary
make check
make docs
```

Focused gate는 product rejection, malformed receipt, artifact/schema/helper mutation,
caller-owned workspace, Stage-1/Stage-2, exact artifact copy, helper binding copy와 raw
execute API 부재를 host fixture에서 검사한다.

이 성공은 host-side execution eligibility와 TOCTOU closure 증거다. Production
signature provider, rollback counter, opcode interpreter, runtime counter, Ribon service
adapter, QEMU 또는 physical hardware policy 실행을 증명하지 않는다.
