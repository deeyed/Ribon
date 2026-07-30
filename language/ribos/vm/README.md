# Ribos VM boundary

`vm/`은 executable artifact verifier와 bounded runtime의 소유 경계다.
`language/ribos/artifact`는 VM ABI 1.0/ISA 1.0 emitter와 allocation-free structural
reader를 제공하지만 VM implementation이나 semantic verification을 소유하지 않는다.

현재 구현된 Stage-1 verifier는 다음 파일에 있다.

```text
include/ribos/vm/verifier.h
src/verifier.c
tools/verify.c
tests/verifier_tests.py
```

Verifier는 caller-owned workspace를 사용하며 frontend나 Policy IR을 링크하지 않고
type/constant, instruction boundary, direct CFG/call, definite slot initialization,
operand/result type와 frame/stack closure를 artifact byte에서 다시 계산한다.

```sh
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
계층의 independent verifier가 type, CFG와 storage closure를 재증명하기 전에는
dispatch할 수 없다. Stage-1 성공도 execution certificate가 아니다. Exact
instruction/helper upper bound, runtime counter, Ed25519 signature와 product key
policy가 실행 허가 전에 별도로 통과해야 한다.

이 README와 host gate는 VM dispatch, boot product linkage, QEMU 또는 hardware policy
실행 증거가 아니다.
