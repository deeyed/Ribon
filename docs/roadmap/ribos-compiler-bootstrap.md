---
doc_type: roadmap
status: accepted
authority: informative
last_verified: 2026-07-30
code_paths:
  - language/ribos/frontend/grammar/
  - language/ribos/frontend/generated/
  - language/ribos/frontend/include/ribos/frontend/
  - language/ribos/frontend/src/
  - language/ribos/frontend/tools/
  - language/ribos/frontend/tests/
  - build/generated/ribos/
  - build/results/
tests:
  - ribos-grammar-generation
  - ribos-parser-conformance
  - ribos-negative-syntax
  - ribos-generated-parser-reproducibility
  - ribos-typed-ast
  - ribos-type-negative
  - ribos-capability-negative
hardware:
  - none
supersedes:
  - none
---

# Ribos compiler bootstrap 프로그램

이 roadmap은 accepted Ribos source contract를 machine-readable grammar와 parser로
내리는 의존 순서를 설명한다. Parser generation은 type checker, verifier, VM과
firmware integration의 증거를 대신하지 않는다.

## G0: Grammar materialization

정본 EBNF를 다음 source로 변환한다.

```text
language/ribos/frontend/grammar/parser.gram
language/ribos/frontend/grammar/Tokens
language/ribos/README.md
```

`parser.gram`은 다음 construct를 모두 구분한다.

- decorator와 top-level declaration
- function, typed parameter와 return
- struct, enum과 payload variant
- block, let, let mut, assignment와 return
- statement if, bounded for와 match
- expression-if
- precedence가 고정된 expression
- positional/named argument
- list, map literal과 struct constructor
- generic-looking bounded type argument
- postfix `?`

`Tokens`는 keyword, reserved keyword, punctuator와 lexical category를 고정한다.
Grammar README는 Pegen source revision, license, generation command와 generated artifact
ownership을 기록한다.

## G1: Host C parser generation

Host parser lane은 CPython Pegen의 grammar reader와 C generator를 사용한다.

```text
.rbs source
  -> Ribos lexer token stream
  -> Pegen-generated C parser
  -> Ribos bounded AST builder
```

Generated C source와 token number header는 다음 tracked operational snapshot이다.

```text
language/ribos/frontend/generated/parser.c
language/ribos/frontend/generated/parser.receipt.json
language/ribos/frontend/generated/tokens.h
```

Generated source를 repository syntax authority로 취급하지 않는다. 일반 build는 이
snapshot을 직접 컴파일하고 grammar/token digest만 검사한다. `.gram`, `Tokens` 또는
generator integration을 바꿀 때 explicit generation target으로 snapshot을
재생성한다.

Generation receipt는 최소한 다음을 기록한다.

```text
grammar digest
token specification digest
Pegen source revision
generator command
generated C digest
compiler identity
generation exit status
```

Host parser lane은 source grammar와 syntax corpus를 빠르게 검증하는 목적이다.
CPython private parser runtime, Python object와 dynamic allocation을 Ribon boot product에
링크하지 않는다.

## G2: Syntax corpus

Positive corpus는 다음 file class를 가진다.

```text
minimal policy
decorated policy
struct와 enum
let과 let mut
multiline expression
statement if와 expression-if
bounded collection
map literal과 named argument
match와 propagation
full boot policy
```

Negative corpus는 다음 failure를 고정한다.

```text
Python conditional expression
exception keyword
while
missing type
unbalanced delimiter
invalid named argument order
empty untyped collection
chained comparison
invalid decorator placement
nested function
top-level statement
```

각 negative fixture는 stable diagnostic category와 primary source span을 가진다.

## G3: Lossless token과 bounded AST

Host front-end는 logical parser token과 source-preserving trivia를 분리하고 Pegen
production action에서 Ribos tagged AST를 직접 만든다.

```text
token:
  kind + byte span + line/column + leading trivia range

trivia:
  space | physical newline | comment

AST:
  numeric node ID + construct kind + source span
  + child relation + operator + inferred type ID
```

AST dump는 raw pointer를 포함하지 않고 parse마다 같은 record ordering을 유지한다.

## G4: AST와 type surface

Parser가 만든 syntax tree를 다음 typed node로 정규화한다.

```text
declaration
statement
expression
type expression
pattern
attribute
source span
```

AST는 Python AST와 호환되지 않는다. Node는 Ribos language construct만 표현하고
CPython object ownership, reference count와 exception state를 포함하지 않는다.

Type stage는 다음 순서로 닫는다.

1. name와 duplicate declaration
2. local inference와 explicit annotation
3. immutable/mutable binding
4. struct와 closed enum
5. bounded collection
6. function call와 named argument
7. exhaustive match
8. Option/Result propagation
9. helper typestate
10. effect, capability와 worst-path helper bound

Worst-path helper bound는 sequential composition, branch maximum, bounded loop multiplier와
reachable user call graph를 포함한다. Recursive call graph는 거부한다.

## G5: Policy IR v1

Typed AST는 frontend와 VM backend 사이의 public Policy IR로 내려간다.

```text
typed virtual slot
explicit basic block
direct branch와 direct user-function/helper call
phi-free explicit move
left-to-right evaluation
checked operator
Option/Result/enum/struct lowering
aggregate shape table
source-map table
helper call-site table
product schema identity
```

Product helper/type/handoff schema는 semantic implementation에서 분리한다.
Product/plugin graph가 versioned canonical schema artifact를 생성하고 compiler와
artifact verifier가 같은 bytes를 소비한다.

AST node 수는 VM instruction 수가 아니다. Exact bytecode instruction budget과 runtime
stack/register allocation은 다음 backend가 소유한다.

## G6: Bytecode와 artifact verifier

Policy IR을 bounded bytecode와 signed artifact로 내린다.

```text
Policy IR validation
  -> deterministic bytecode selection
  -> exact instruction/call/stack bound
  -> schema digest와 capability manifest
  -> source/helper map
  -> canonical artifact serialization
  -> signature와 rollback metadata
```

Artifact verifier는 compiler를 신뢰하지 않고 control flow, register/stack type,
helper signature/capability, loop/call bound와 terminal action을 다시 증명한다.

## G7: VM과 semantic helper dispatch

VM은 verified artifact만 실행한다. Runtime은 instruction, call depth, stack, helper와
output budget을 재강제한다. Checked arithmetic, invalid tag, deadline과 helper failure는
exception unwind가 아니라 typed result 또는 fail-closed policy fault다.

Helper stable ID는 product-generated dispatch table로 해석한다. Policy가 native
function pointer, raw address, raw MMIO 또는 raw flash primitive를 만들 수 없다.

## G8: 선택적 standalone fixed parser backend

Firmware parser를 선택하는 product는 Pegen grammar를 공유하되 CPython C runtime을
사용하지 않는다. Ribon C backend는 다음 storage를 caller-owned context에 둔다.

```text
fixed token array
fixed AST arena
fixed list-item arena
fixed memo table
fixed diagnostic array
immutable source byte span
```

다음 generator emission을 Ribon primitive로 치환한다.

| CPython C emission | Ribon emission |
| --- | --- |
| `PyMem_Malloc/Realloc/Free` | fixed arena allocation과 capacity failure |
| `asdl_seq` | indexed fixed sequence span |
| `PyObject` token value | source offset/length와 token kind |
| `PyErr_*` | stable bounded diagnostic |
| `PyArena` memo node | fixed memo slot |
| Python identifier cache | bounded symbol table 또는 source slice |

Grammar의 repetition과 gather는 product limit를 넘어갈 때
`RIBOS_PARSE_LIMIT_EXCEEDED`로 종료한다. Heap fallback은 없다.

Production product가 signed artifact만 설치한다면 G8 parser를 firmware에 포함하지
않는다. Interactive development shell 또는 on-device source provisioning product만
이 lane을 선택한다.

## G9: Compiler/runtime acceptance gate

전체 language/runtime acceptance는 다음을 서로 다른 evidence lane으로 요구한다.

```text
Pegen grammar validation pass
deterministic C source generation
generated host C parser compile
positive corpus parse pass
negative corpus stable rejection pass
formatter parse-format-parse equality
deterministic token/trivia와 typed AST dump
type, mutation와 collection negative corpus
match, propagation와 typestate corpus
capability, pure-effect, recursion과 budget negative corpus
canonical product schema identity와 mismatch rejection
deterministic Policy IR와 structural negative corpus
bytecode reproducibility와 adversarial verifier corpus
VM instruction/helper/stack budget fault corpus
factory recovery fault injection
repository documentation lint pass
Sphinx warnings-as-errors pass
```

Host frontend와 Policy IR gate만으로 다음을 주장하지 않는다.

- standalone no-heap firmware parser
- bytecode generation
- static verifier completion
- policy VM execution
- Ribon boot product linkage
- QEMU 또는 physical hardware policy execution

## Remaining compiler/runtime closure

Policy IR 뒤의 독립 작업은 bytecode emission, signed artifact format, adversarial
static verifier, VM execution과 Ribon helper integration이다. Fixed-storage parser는
source를 firmware에서 받아야 하는 product만 선택한다.

Host compiler 성공을 firmware-ready parser, artifact verifier 또는 VM execution으로
표현하지 않는다.
