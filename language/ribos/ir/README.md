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

IR은 Pegen token, AST pointer, frontend internal enum과 VM register를 포함하지 않는다.
현재 module storage는 host compiler용 fixed-capacity heap object다. Serialized policy
artifact, bytecode emission, static verifier와 VM dispatch는 후속 계층의 책임이다.
