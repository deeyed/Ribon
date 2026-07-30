# Ribos Policy IR

`ir/`는 frontend와 bytecode/VM backend 사이의 public Policy IR v1 계약이다.

Policy IR v1은 다음을 직접 표현한다.

- function-owned typed virtual slot
- explicit basic block와 direct branch target
- direct user-function call과 schema stable-ID helper call
- explicit `move`를 이용한 phi-free merge
- left-to-right expression evaluation order
- checked integer operator ID
- `Option`, `Result`, user enum과 struct construction/destruction
- user aggregate shape table
- source-map table
- helper call-site table
- canonical product schema SHA-256 identity
- explicit bounded-loop table
- reachable CFG와 terminal closure
- type/slot/frame/stack storage layout
- instruction, call depth와 helper별 worst-path upper bound

IR은 Pegen token, AST pointer, frontend internal enum과 VM register를 포함하지 않는다.
`analysis.h`는 frontend AST 없이 Policy IR v1.1의 resource closure를 계산한다.
List는 inline capacity storage, Dict/FrozenMap은 stable-order sorted array와 bounded
linear search layout을 사용한다. Compiler는 분석값으로 source budget을 집행한다.

현재 module과 closure storage는 host compiler용 bounded heap object다. Serialized
policy artifact, independent static verifier와 VM counter dispatch는 후속 계층의
책임이다.
