---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/host/tools/run.c
  - language/ribos/host/tests/fixture_codec.py
  - language/ribos/host/tests/replay_tests.py
  - language/ribos/host/tests/conformance_tests.py
  - language/ribos/host/tests/hostile_tests.py
  - language/ribos/vm/
tests:
  - make check-ribos-host-tools
  - make check-ribos-replay
  - make check-ribos-conformance
  - make check-ribos-hostile
  - make check-ribos-vm
  - make check
hardware:
  - none
supersedes:
  - ad-hoc Ribos VM host harness
---

# Ribos deterministic host replay v1 계약

## 목적과 증거 경계

`ribos-run`은 `.rbs` source를 해석하는 두 번째 VM이나 test-only reference
interpreter가 아니다. `ribosc`가 만든 `.rba`를 독립 verifier로 준비하고 target에도
링크되는 `libribos-target-core.a`의 production VM entry를 hosted embedder에 연결한다.

```text
SOURCE.rbs
    |
    v
ribosc -------------------------- host compiler
    |
    v
POLICY.rba
    |
    +--> ribos-verify ----------- independent verifier
    |
    +--> ribos-run
            |
            +--> authorized/prepared artifact
            +--> production target-core VM
            +--> CONTEXT.rbctx
            +--> HELPERS.rbtr
            |
            v
        RIBOS-RUN-REPORT-V1
```

이 계약은 compiler-to-runtime host end-to-end, deterministic replay, opcode
conformance와 hostile-input resilience를 증명한다. Production signature/key,
rollback, Ribon product service, network, storage, QEMU, 실제 firmware와 hardware
실행 증거는 아니다.

## Host tool 이름

사용자-facing tool 이름은 다음으로 고정한다.

| Tool | 입력 | 출력 |
| --- | --- | --- |
| `ribosc` | `.rbs`, selected schema | canonical `.rba` |
| `ribos-verify` | untrusted `.rba`, selected schema | independent verifier report |
| `ribos-run` | `.rba`, `.rbctx`, `.rbtr` | deterministic text report |

일반 build는 Pegen을 실행하지 않는다. `ribos-run`은
`libribos-target-core.a`를 링크하고 `libribos-host-compiler.a`를 링크하지 않는다.

## Context snapshot wire v1

Context fixture는 C structure image가 아니라 little-endian byte format이다. Header
크기는 128 byte이고 payload는 entry `BootContext` type의 exact VM value encoding이다.

| Offset | Size | Field |
| --- | ---: | --- |
| 0 | 8 | `RBCTX1\0\0` |
| 8 | 2 | major = 1 |
| 10 | 2 | minor = 0 |
| 12 | 4 | header bytes = 128 |
| 16 | 4 | flags = 0 |
| 20 | 4 | context type ID |
| 24 | 4 | selected mode |
| 28 | 4 | selected phase |
| 32 | 8 | nonzero generation |
| 40 | 8 | payload offset = 128 |
| 48 | 8 | payload length |
| 56 | 8 | total length |
| 64 | 32 | context snapshot SHA-256 |
| 96 | 32 | zero reserved |

Context snapshot digest는 header byte `0..63`과 payload를 순서대로 SHA-256한 값이다.
따라서 type, mode, phase, generation, range와 payload가 하나의 identity로 묶인다.
Payload만 같아도 mode, phase 또는 generation이 다르면 같은 snapshot이 아니다.

## Helper transcript wire v1

Transcript header는 192 byte다.

| Offset | Size | Field |
| --- | ---: | --- |
| 0 | 8 | `RBTRN1\0\0` |
| 8 | 2 | major = 1 |
| 10 | 2 | minor = 0 |
| 12 | 4 | header bytes = 192 |
| 16 | 4 | flags = 0 |
| 20 | 4 | row count |
| 24 | 4 | row bytes = 128 |
| 28 | 4 | zero reserved |
| 32 | 8 | row offset = 192 |
| 40 | 8 | row byte length |
| 48 | 8 | payload offset |
| 56 | 8 | payload byte length |
| 64 | 8 | total length |
| 72 | 32 | bound artifact payload SHA-256 |
| 104 | 32 | bound context snapshot SHA-256 |
| 136 | 32 | rows-plus-payload SHA-256 |
| 168 | 24 | zero reserved |

각 helper row는 호출 순서와 callback 결과를 exact하게 기록한다.

| Offset | Size | Field |
| --- | ---: | --- |
| 0 | 8 | 1부터 시작하는 sequence |
| 8 | 4 | helper stable ID |
| 12 | 4 | callback status |
| 16 | 4 | result kind |
| 20 | 4 | flags = 0 |
| 24 | 8 | consumed operation count |
| 32 | 8 | consumed poll count |
| 40 | 8 | elapsed nanoseconds |
| 48 | 8 | transcript payload-relative offset |
| 56 | 8 | result payload length |
| 64 | 4 | journal receipt state |
| 68 | 4 | zero reserved |
| 72 | 32 | journal receipt digest |
| 104 | 8 | host object ID for handle result |
| 112 | 16 | zero reserved |

Row payload은 순서대로 빈틈 없이 이어져야 한다. Operation, poll, elapsed와 total
row 수에는 hard maximum이 있다. Callback status는 `OK`, typed `POLICY_ERROR`,
`CONTRACT_FAULT`만 허용한다. Result는 value, handle, typed policy error 또는
contract fault의 no-result 중 callback status와 일치하는 하나다.

Journaled helper는 `COMMITTED`, `PARTIAL` 또는 `UNCERTAIN` receipt와 nonzero digest를
반드시 제공한다. 다른 effect class에 journal receipt를 넣을 수 없다. Handle result는
nonzero object ID를 가지며 VM의 generation token 외부에서 bytecode value를 위조하지
않는다.

## Hosted helper execution contract

Host runner는 selected reference product schema와 artifact의 실제 helper import를
이용해 versioned execution descriptor를 만든다.

- terminal schema flag는 `TERMINAL` effect와 sealed-intent durability다.
- `DEVICE`, `STATE`, `FLASH`, `HANDOFF` capability는 journaled effect다.
- `NETWORK`, `DIAGNOSTIC` capability는 ephemeral effect다.
- 나머지는 pure effect다.
- schema typestate와 result ownership으로 create, consume, replace 또는 terminal
  consume transition을 정한다.
- artifact가 helper를 import하지 않으면 ABI가 요구하는 non-empty table을 위해
  unreachable `boot.recovery` descriptor 하나만 넣는다.

이 table은 test fixture가 helper 의미를 새로 정의하는 것이 아니다. Schema의
signature, capability, ownership과 transition을 보존하고 operation/duration/result만
transcript에서 공급한다.

Host development authorizer는 signature가 없는 local artifact만 승인한다. 이는
production signature verification이 아니며 signed artifact, root key와 rollback
decision을 승인하지 않는다.

## 결정론과 report

동일한 다음 tuple은 byte-for-byte 같은 report를 만들어야 한다.

```text
(artifact bytes, context snapshot bytes, helper transcript bytes)
```

Report는 path, host pointer, process time와 allocator address를 포함하지 않는다.
다음 class를 고정된 key order로 기록한다.

- artifact, schema, prepared binding, context와 transcript digest
- `BootAction`, `PolicyError` 또는 `VmFault`
- output type, stable error code, payload length와 digest
- actual/verifier-upper instruction과 helper count
- operation, poll, stack, frame와 call-depth 값
- source position
- terminal action, journal chain과 trace digest
- fault code, subject, instruction와 helper
- recovery call count와 transcript exact-consumption count
- report 본문 SHA-256

Source map은 진단 정보다. 같은 policy의 source 위치만 이동해도 artifact/report
identity와 source field는 달라지지만 outcome, resource count와 terminal 의미는
같아야 한다. Host compiler의 v1 canonical output은 source-map section을 포함한다.
Source-map section 자체를 생략한 artifact는 structural codec format에는 정의되어
있지만 host replay v1 accepted execution profile에는 포함하지 않는다.

## Fail-closed 규칙

- Artifact hash, context identity 또는 transcript body hash가 다르면 실행하지 않는다.
- Transcript의 artifact/context binding이 다르면 실행하지 않는다.
- Helper stable ID, result type/size, journal class와 row 순서가 다르면 VM fault 또는
  host fixture rejection으로 닫는다.
- Transcript row를 남기거나 추가 callback이 transcript 밖으로 나가면 report를
  만들지 않는다.
- VM fault는 terminal snapshot과 trace를 봉인한 뒤 factory recovery callback을
  정확히 한 번 호출한다.
- Source map은 fault 위치를 설명할 뿐 dispatch, counter와 outcome authority가 아니다.

## Gate와 해석

```sh
make check-ribos-host-tools
make check-ribos-replay
make check-ribos-conformance
make check-ribos-hostile
make check-ribos-vm
```

`check-ribos-replay`는 action, typed error와 journaled action을 각각 네 번 실행한다.
`check-ribos-conformance`는 ISA v1 24개 opcode를 모두 실행 corpus에 포함하고
compiler, verifier, runtime의 instruction/helper/stack/call-depth 상한을 비교한다.
`check-ribos-hostile`은 deterministic partial-journal fault, 72개 byte mutation,
15개 truncation과 valid-hash invalid-opcode/wrong-helper 입력을 bounded timeout으로
검사한다. Sanitizer build는 host-only 추가 증거이며 target firmware 증거로 승격할
수 없다.
