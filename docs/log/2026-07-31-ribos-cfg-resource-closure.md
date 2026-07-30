---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-07-31
code_paths:
  - language/ribos/ir/include/ribos/ir/analysis.h
  - language/ribos/ir/src/analysis.c
  - language/ribos/frontend/src/lower.c
  - language/ribos/frontend/tools/parse.c
  - Makefile
tests:
  - make check-ribos-resources
  - make check-ribos-semantics
  - make check-ribos-ir
  - sanitizer Ribos resource corpus
  - make check
  - make docs
hardware:
  - not-run
supersedes:
  - none
---

# Ribos CFG와 resource closure 구현 기록

## Policy IR v1.1

Policy IR에 explicit bounded-loop row와 product named type의 VM ABI size/alignment를
추가했다. Loop row는 function, header, body, exit, latch, trip count와 source map을
결합한다. Validator는 header branch, optional latch jump와 trip count가 일치하지 않으면
module을 거부한다.

Frontend는 `for`를 낮출 때 loop row를 생성한다. Parser와 typed AST는 resource analyzer
public dependency가 아니며, analyzer는 opaque `RibosIrModule`만 입력으로 받는다.

## CFG closure

Host analyzer는 function entry에서 block reachability를 계산하고 다음을 닫는다.

- reachable `RETURN`과 fail-closed `TRAP`
- annotation 없는 CFG cycle rejection
- bounded loop와 nested loop trip count
- acyclic direct-call graph와 maximum call depth
- Policy IR instruction worst-path upper bound
- 전체 helper와 stable-ID별 worst-path upper bound

Nested `2×3` loop corpus에서 `device.init` helper upper bound가 6인지 독립적으로
검사한다. Structural unit은 loop row가 없는 two-block cycle을 거부한다.

## Storage closure

Host ABI와 독립적인 type layout을 계산하고 virtual slot ID 순서로 frame offset을
배치한다. Function별 frame byte, aggregate slot byte, largest value, maximum call-stack
byte를 기록한다.

List는 `u32 length`와 inline capacity array다. FrozenMap과 Dict는 `u32 cardinality`와
stable-order fixed-capacity entry array이며 lookup model은 bounded linear search다.

## Budget gate

모든 semantic compile은 temporary 또는 caller-owned Policy IR까지 낮아져 resource
closure를 수행한다. Worst-path instruction upper bound가 `instruction_budget`을 넘으면
`E_INSTRUCTION_BUDGET_EXCEEDED`로 compile failure가 된다. Partial module은 reset된다.

`--dump-resources` inspection mode는 type, function, block, loop, slot과 helper bound
table을 pointer identity 없이 결정론적으로 출력한다.

## Host verification

기본 host toolchain과 Apple Clang의 AddressSanitizer/UndefinedBehaviorSanitizer
조합에서 schema, parser, semantics, Policy IR와 resource closure corpus를 실행했다.
Sanitizer 실행은 `ASAN_OPTIONS=detect_leaks=0`이므로 leak 검출 증거를 주장하지
않는다.

관찰한 핵심 marker는 다음과 같다.

- `RIBOS-IR-RESOURCE-TEST-OK instructions=2 frame=0 cycle-rejected=1 budget-enforced=1`
- `RIBOS-RESOURCE-CLOSURE-OK fixtures=5 deterministic=1 cfg=closed budgets=enforced dict=sorted-array`
- `RIBOS-SEMANTIC-CORPUS-OK positive=5 negative=18 deterministic-dump=1`
- `RIBOS-POLICY-IR-V1-OK fixtures=5 ... opcodes=21 deterministic=1`

## Evidence boundary

이 변경의 실행 증거는 host compile/unit/corpus와 documentation build에 한정된다.
Bytecode serialization, hostile-byte static verifier, VM dispatch counter, Ribon product
linkage, QEMU와 physical hardware policy execution은 수행하지 않았다.
