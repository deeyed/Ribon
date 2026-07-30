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

IR은 Pegen token, AST pointer와 frontend internal enum을 포함하지 않는다.
`analysis.h`는 frontend AST 없이 Policy IR v1.1의 resource closure를 계산한다.
List는 inline capacity storage, Dict/FrozenMap은 stable-order sorted array와 bounded
linear search layout을 사용한다. Compiler는 분석값으로 source budget을 집행한다.

`ribos_ir_module_view()`는 validated host module table을 artifact emitter에 borrowed
read-only view로 공개한다. 이 view와 host structure layout은 wire ABI가 아니다.
`language/ribos/artifact`만 explicit little-endian `.rba` serialization을 소유한다.

현재 module과 closure storage는 caller-supplied allocator를 사용하는 host compiler용
bounded object다. IR dump도 explicit writer를 받는다. Independent hostile-byte
verifier와 VM counter dispatch는 `vm/` 계층의 책임이다.
