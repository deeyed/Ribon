# Ribos VM boundary

`vm/`은 향후 bytecode emitter, executable artifact verifier와 bounded runtime의
소유 경계다. Policy IR v1 라운드에는 VM implementation을 넣지 않는다.

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

이 README는 implementation 완료 증거가 아니라 dependency와 attack-surface
경계다.
