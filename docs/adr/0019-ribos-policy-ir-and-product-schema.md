---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-30
code_paths:
  - language/ribos/frontend/
  - language/ribos/schema/
  - language/ribos/ir/
  - language/ribos/vm/
tests:
  - make check-ribos-parser-pilot
  - make check-ribos-semantics
  - make check-ribos-schema
  - make check-ribos-ir
  - make check
  - make docs
hardware:
  - none
supersedes:
  - .ribos source extension
  - monolithic Ribos parser, compiler, IR and VM project boundary
  - semantic.c-owned product helper schema
---

# ADR 0019: Ribos Policy IR와 product schema 경계

## Context

Typed frontend만으로는 VM backend와 artifact verifier가 source AST 없이 control flow,
value type, helper authority와 product identity를 재검사할 수 없다. AST node와
frontend operator enum을 bytecode로 직접 변환하면 parser implementation이 runtime
ABI에 새어 나오고 VM 변경이 source compiler를 강하게 결합한다.

기존 helper/type/fact schema는 `semantic.c`의 C table이었다. Compiler만 이 table을
알면 다음 문제가 생긴다.

- verifier가 compiler와 같은 helper signature와 capability를 독립적으로 확인할 수
  없다.
- 두 product의 같은 helper spelling이 다른 의미를 가져도 artifact identity가 이를
  구분하지 못한다.
- product/plugin graph가 선택한 helper set과 compiler built-in table이 drift한다.
- C pointer, link order 또는 structure padding을 artifact identity로 오용할 위험이
  있다.

Source extension과 project hierarchy도 frontend, IR와 VM의 독립 배포·검증 경계를
명시해야 한다.

## Decision

Ribos source extension은 `.rbs`로 고정한다. `.ribos` alias와 하위 호환 loader는
제공하지 않는다.

Language project는 다음 네 경계로 분리한다.

```text
frontend  .rbs -> token/trivia -> AST -> typed AST -> IR builder calls
schema    product descriptor -> canonical schema bytes -> SHA-256 identity
ir        typed slots + explicit CFG + source/helper/shape tables
vm        verified artifact -> bytecode execution (후속 구현)
```

Frontend private AST와 Pegen runtime은 `frontend/` 밖의 public dependency가 아니다.
Bytecode emitter, artifact verifier와 VM은 public Policy IR/schema contract만 소비한다.

Policy IR v1은 다음 표현을 가진다.

- function-owned typed virtual slot
- explicit basic block와 direct branch target
- direct user-function call
- schema stable ID를 target으로 하는 direct helper call
- block merge에서 phi 대신 explicit `move`
- source 순서와 같은 left-to-right evaluation
- frontend operator와 분리된 checked operator ID
- `Option`, `Result`, user enum과 struct lowering
- user aggregate shape table
- stable source-map table
- helper call-site table
- canonical product schema SHA-256 identity

IR module은 pointer identity를 dump나 계약에 포함하지 않는다. 현재 module C storage는
host compiler 내부 표현이며 signed artifact wire format이 아니다.

Product schema는 versioned canonical little-endian byte encoding을 가진다. Table row는
stable ID 오름차순이며 product ID, type, member, helper signature/capability와 handoff
field를 포함한다. Identity는 canonical bytes의 SHA-256이고 native C layout을
hash하지 않는다.

현재 generic reference schema는 host corpus용이다. 장기 product build의 권위는
product/plugin graph가 생성하는 하나의 schema artifact다. Host compiler와 artifact
verifier는 그 동일 artifact를 소비하고 policy artifact는 digest를 봉인한다.

## Consequences

- Source parser와 VM runtime의 attack surface가 분리된다.
- Frontend를 바꾸어도 Policy IR v1 contract를 보존하면 verifier/VM backend를
  재사용할 수 있다.
- VM backend는 AST 없이 user aggregate의 field, variant와 payload shape를 알 수 있다.
- Helper call-site가 source span, schema stable ID, capability와 IR instruction ID를
  함께 가진다.
- Product schema가 다르면 source가 같아도 artifact identity와 검증 결과가 다르다.
- `.ribos` file은 active language tree에서 허용되지 않는다.
- Bytecode serialization, exact VM instruction budget, artifact signature, static
  verifier와 runtime dispatch는 이 ADR의 완료 범위가 아니다.

## 기각한 대안

### AST를 VM bytecode로 직접 변환

Frontend private node/operator 의미를 runtime ABI로 만들고 independent verifier 입력이
없어지므로 채택하지 않는다.

### Helper schema를 compiler C table로 유지

Product graph와 verifier가 같은 authority artifact를 소비할 수 없으므로 채택하지
않는다.

### LLVM IR 또는 WebAssembly를 Policy IR로 사용

Ribos의 helper capability, product schema identity, source-level bounded aggregate와
fail-closed checked arithmetic을 별도 metadata와 verifier로 다시 정의해야 한다.
Round 1의 작은 semantic IR 경계로는 채택하지 않는다.
