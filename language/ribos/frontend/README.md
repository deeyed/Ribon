# Ribos frontend

`frontend/`는 UTF-8 `.rbs` source를 lossless token/trivia, bounded AST와 typed semantic
model로 변환한다. Pegen grammar action은 Ribos AST를 직접 생성하며 CPython AST와
Python runtime object를 만들지 않는다.

Frontend public API는 `include/ribos/frontend/`에 있다. `parser_internal.h`와
`semantic_internal.h`는 frontend 밖에서 include할 수 없다.

`src/lower.c`는 typed AST를 public Policy IR builder로 내리는 유일한 bridge다.
이 파일은 VM register, bytecode encoding, runtime stack과 dispatch를 알지 않는다.
Bytecode artifact emitter는 frontend private AST 대신 `ribos/ir/ir.h`와
`ribos/ir/analysis.h`만 소비한다. 모든 successful semantic compile은 Policy IR
resource closure까지 수행해 declared instruction/helper budget을 검사한다.

Host inspection CLI는 `--emit-artifact OUTPUT.rba SOURCE.rbs`로 source-map을 포함한
unsigned VM ABI 1.0 artifact를 만들고 즉시 structural reader로 재개방한다. Production
signature policy와 independent bytecode verification은 frontend의 책임이 아니다.

정상 build는 `generated/` snapshot을 사용한다. `grammar/` 또는 generator integration
변경이 없으면 Pegen을 다시 실행하지 않는다.
