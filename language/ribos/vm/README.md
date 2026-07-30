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
계층의 independent verifier 두 단계를 통과하기 전에는 dispatch할 수 없다. Stage-2는
좁은 의미의 compiler/verifier semantic closure이지만 execution certificate는
아니다. Runtime counter, Ed25519 signature, rollback과 product key policy가 실행
허가 전에 별도로 통과해야 한다.

이 README와 host gate는 VM dispatch, boot product linkage, QEMU 또는 hardware policy
실행 증거가 아니다.
