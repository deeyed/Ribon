# Ribos frontend

`frontend/`는 UTF-8 `.rbs` source를 lossless token/trivia, bounded AST와 typed semantic
model로 변환한다. Pegen grammar action은 Ribos AST를 직접 생성하며 CPython AST와
Python runtime object를 만들지 않는다.

Frontend public API는 `include/ribos/frontend/`에 있다. `parser_internal.h`와
`semantic_internal.h`는 frontend 밖에서 include할 수 없다.

`src/lower.c`는 typed AST를 public Policy IR builder로 내리는 유일한 bridge다.
이 파일은 VM register, bytecode encoding, runtime stack과 dispatch를 알지 않는다.
향후 bytecode backend와 verifier는 frontend private AST 대신 `ribos/ir/ir.h`만
소비한다.

정상 build는 `generated/` snapshot을 사용한다. `grammar/` 또는 generator integration
변경이 없으면 Pegen을 다시 실행하지 않는다.
