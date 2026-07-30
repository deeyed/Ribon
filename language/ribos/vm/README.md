# Ribos VM boundary

`vm/`은 executable artifact verifier와 bounded runtime의 소유 경계다.
`language/ribos/artifact`는 VM ABI 1.0/ISA 1.0 emitter와 allocation-free structural
reader를 제공하지만 VM implementation이나 semantic verification을 소유하지 않는다.

구현된 two-stage verifier는 다음 파일에 있다.

```text
include/ribos/vm/verifier.h
src/verifier.c
../host/tools/verify.c
tests/verifier_tests.py
```

Verifier는 caller-owned workspace를 사용하며 frontend나 Policy IR을 링크하지 않는다.
Stage-1은 type/constant, instruction boundary, direct CFG/call, definite slot
initialization, operand/result type와 frame/stack closure를 다시 계산한다. Stage-2는
helper ABI와 schema digest, reachable capability, opaque provenance, affine/linear
ownership, typestate transition, exact instruction/helper bound, helper별 bound와
terminal/fault closure를 artifact byte에서 다시 계산한다.

Verifier, target-safe artifact codec, schema와 neutral base adapter는
`libribos-target-core.a`에 들어간다. CLI만 host 계층에 있으며 target archive에는
libc allocator, `FILE`, frontend, Policy IR와 artifact emitter가 들어갈 수 없다.

Runtime public ABI는 다음 header가 소유한다.

```text
include/ribos/vm/runtime.h
include/ribos/vm/prepared.h
include/ribos/vm/storage.h
include/ribos/vm/handles.h
include/ribos/vm/helpers.h
include/ribos/vm/interpreter.h
include/ribos/vm/terminal.h
```

Runtime ABI 1.0은 product schema 1.1과 helper execution contract 1.0을 분리한다.
Schema는 type, helper signature, capability와 ownership을 소유하고 execution contract는
effect, synchronous callback, mode/phase, I/O, operation, poll, deadline, durability와
handle transition을 소유한다. `RibosPreparedProgram`과 `RibosVmHelperCall`은 opaque
process-local type이며 public structure는 fixed-width field와 explicit pointer만
사용한다.

`prepared.h`는 raw artifact와 execution 사이의 authorization/preparation lifetime을
제공한다. Product callback이 signature, key와 rollback authority를 소유하고 generic
VM은 copied artifact에 Stage-1과 Stage-2를 실행한다. 성공한
`RibosPreparedProgram`은 exact artifact byte, copied helper callback table, verifier
report, effective limits와 artifact/schema/helper binding digest를 caller-owned
workspace에 봉인한다. Stage-2가 승인한 artifact type별 ownership/schema class도
복사해 runtime handle과 aggregate interpreter가 같은 의미를 소비한다. Selected
schema pointer와 original helper table pointer는 보존하지 않는다.

`storage.h`는 PreparedProgram의 verifier closure와 product/mode limit을 하나의
caller-owned 8-byte-aligned runtime arena plan으로 낮춘다. Frame, typed slot state,
loop/helper counter, handle, aggregate scratch, outcome/output, fault와 optional trace는
fixed offset을 가지며 heap과 native C value union을 사용하지 않는다. Scalar slot은
artifact type width를 다시 확인하고 explicit little-endian byte로 읽고 쓴다.

`interpreter.h`는 PreparedProgram만 받는 target-neutral incremental engine이다.
Verified instruction ID를 PC로 사용하고 dispatch 전에 fuel을 감소시키며 parameter,
scalar constant, move, checked unary/binary, direct jump/branch, direct call, return과
trap을 portable C switch로 실행한다. List, Dict, struct와 tagged variant는 verified
fixed-capacity inline layout 안에서만 구성·조회하며 unused capacity와 padding을
결정론적으로 zero한다. Dict는 stable key order의 fixed entry array와 bounded linear
lookup을 쓴다. Policy call은 C recursion 대신 arena의 explicit frame record를 쓰고
8-byte-aligned frame size와 verified depth/stack closure를 push 전에 검사한다. Scalar와
aggregate argument/result는 exact slot byte로 전달하고 non-copy source 전체를
`MOVED`로 바꾼다. Loop는 verified
latch-to-header edge에서만 fixed counter를 감소시킨다. Context
generation/type/digest와 fault receipt도 arena에 fixed-offset byte로 봉인한다. 이
engine의 entry `RETURNED`는 내부 함수 반환이며 `BootAction`이나 full-policy success가
아니다. `handles.h`는 fixed arena record와 process-local host table을 결합한
generation token, synchronous borrow, irreversible consume/replace/revoke와 bounded
fault cleanup을 제공한다. `helpers.h`와 helper-aware interpreter API는
PreparedProgram의 copied stable-ID binding을 typed synchronous callback에 연결하고
generation handle, total/per-helper call, input/output, operation/poll, deadline과
callback lifetime을 집행한다. Helper authority가 없는 base interpreter API는
`CALL_HELPER`를 계속 fail closed한다.

Execute의 terminal 결과는 sealed `BootAction`, typed `PolicyError`, catch 불가능한
`VmFault` 세 class뿐이다. BootAction은 실제 jump가 아닌 single-consume intent이며
`terminal.h`의 고수준 executor만 full-policy success를 봉인한다. Terminal helper의
결과는 entry 함수가 `Result::Ok`로 같은 action handle을 반환한 뒤에만 action
receipt가 되며, `Result::Err`는 typed PolicyError로 닫힌다. Journaled helper는
product callback이 제공한 committed receipt를 먼저 저장하고 terminal journal
chain에 포함한다. 이후 fault가 나더라도 이미 발생한 외부 effect를 rollback했다고
주장하지 않고 `PARTIAL` transaction state와 receipt chain을 보존한다. Fault recovery
callback은 handle cleanup, output zero, trace digest와 terminal snapshot을 먼저
봉인한 뒤 정확히 한 번 통지할 뿐 outcome을 바꾸거나 policy를 재실행하지 않는다.

```sh
make check-ribos-runtime-contract
make check-ribos-prepared-program
make check-ribos-runtime-storage
make check-ribos-vm-scalar
make check-ribos-vm-calls
make check-ribos-vm-loops
make check-ribos-vm-aggregates
make check-ribos-vm-handles
make check-ribos-vm-helpers
make check-ribos-vm-terminal
make check-ribos-vm-faults
make check-ribos-verifier
build/tools/ribos-verify POLICY.rba
```

VM 계층은 다음 규칙을 지켜야 한다.

- frontend private header, Pegen과 `.rbs` source를 링크하지 않는다.
- selected product schema artifact와 artifact에 봉인된 identity가 같은지 재검사한다.
- verified direct branch/call과 typed helper call만 실행한다.
- checked arithmetic overflow, divide-by-zero와 invalid shift를 catchable exception이
  아닌 fail-closed policy fault로 처리한다.
- instruction, call depth, stack, helper와 output budget을 runtime에서도 강제한다.
- resource closure가 봉인한 initial instruction/helper counter를 dispatch 전에
  검사하고 감소시킨다.
- Dict/FrozenMap은 stable-order fixed-capacity array와 bounded linear search로
  실행한다.
- helper stable ID를 product-generated dispatch table에 연결하고 raw function pointer,
  raw MMIO, raw flash와 arbitrary jump를 policy에 노출하지 않는다.

Structural reader가 envelope, payload hash와 section range를 통과시킨 artifact도 이
계층의 independent verifier 두 단계를 통과하기 전에는 dispatch할 수 없다. Stage-2는
좁은 의미의 compiler/verifier semantic closure이지만 execution certificate는
아니다. Runtime counter, Ed25519 signature, rollback과 product key policy가 실행
허가 전에 별도로 통과해야 한다.

이 README와 host gate는 runtime ABI, verifier, host-side
scalar/direct-call/loop/aggregate dispatch, generation handle defense, fake-embedder
typed helper execution, terminal action sealing과 fail-closed recovery를 설명한다.
이는 실제 실행 권한 이전, production signature/rollback provider, Ribon product
service linkage, QEMU 또는 hardware policy 실행 증거가 아니다.
