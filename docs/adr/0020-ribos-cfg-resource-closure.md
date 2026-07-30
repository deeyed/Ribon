---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/ir/include/ribos/ir/analysis.h
  - language/ribos/ir/src/analysis.c
  - language/ribos/frontend/src/lower.c
tests:
  - make check-ribos-resources
  - make check-ribos-semantics
  - make check
hardware:
  - none
supersedes:
  - Policy IR resource analysis deferral
---

# ADR 0020: Ribos CFG와 정적 resource closure

## Context

Typed AST의 node 수와 source-level helper estimate만으로는 VM frame, reachable
instruction, loop back-edge와 bytecode runtime budget을 증명할 수 없다. 특히 다음
경우 source syntax만 세면 실행 상한이 잘못된다.

- short-circuit와 mutually exclusive branch
- nested bounded loop
- direct user-function call
- propagation이 만드는 early return
- unreachable synthetic block
- List와 Dict의 capacity-dependent storage와 lookup

VM 구현까지 budget 계산을 미루면 compiler, artifact verifier와 runtime이 서로 다른
상한을 사용하게 된다.

## Decision

Policy IR v1 minor version을 `1.1`로 올리고 bounded loop table과 product-named type의
ABI size/alignment를 추가한다.

`language/ribos/ir`은 frontend와 VM에 독립적인 resource analyzer를 소유한다.
Analyzer는 다음을 결정론적으로 계산한다.

- entry-based block reachability
- 모든 path의 `RETURN`/`TRAP` closure
- loop trip count와 unannotated cycle rejection
- type와 virtual-slot layout
- function frame와 maximum call-stack byte
- acyclic direct-call depth
- worst-path Policy IR instruction 상한
- 전체 helper와 stable-ID별 call 상한

Compiler는 resource closure 뒤 `instruction_budget`과 `helper_budget`을 실제 compile
gate로 사용한다. Runtime VM은 후속 artifact에 봉인된 같은 값을 counter로 다시
집행한다.

Collection은 inline bounded storage를 사용한다. Dict와 FrozenMap은 stable total
order의 정렬 배열과 bounded linear search로 고정한다. Randomized hash table과 resize
fallback은 사용하지 않는다.

## Consequences

- AST node count를 instruction budget으로 오인하지 않는다.
- Loop/call/helper 상한은 frontend private state 없이 재계산할 수 있다.
- Frame layout은 host C ABI가 아니라 artifact verifier가 재현 가능한 값이다.
- Dictionary lookup의 worst-case 비교 수는 capacity로 설명할 수 있다.
- Slot liveness reuse가 없어 frame이 커질 수 있지만 verifier baseline은 단순하고
  결정론적이다.
- Policy IR analysis 성공은 VM 실행이나 bytecode verifier 완료를 의미하지 않는다.

## 기각한 대안

### Source AST에서 instruction budget 계산

한 AST construct가 여러 CFG instruction으로 낮아지고 backend 경계도 침범하므로
채택하지 않는다.

### 평균 O(1) hash Dict

Hash seed, collision과 resize가 Pre-OS worst-case latency와 storage 설명을 어렵게 하므로
채택하지 않는다.

### Host compiler 결과를 artifact metadata로 신뢰

Compiler compromise나 drift를 독립 verifier가 발견할 수 없으므로 채택하지 않는다.
