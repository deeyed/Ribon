---
doc_type: roadmap
status: accepted
authority: informative
last_verified: 2026-07-30
code_paths:
  - language/grammar/
  - language/generated/
  - tools/ribosc/
  - tests/language/
  - build/generated/ribos/
  - build/results/
tests:
  - ribos-grammar-generation
  - ribos-parser-conformance
  - ribos-negative-syntax
  - ribos-generated-parser-reproducibility
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
language/grammar/ribos.gram
language/grammar/Tokens
language/grammar/README.md
```

`ribos.gram`은 다음 construct를 모두 구분한다.

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
.ribos source
  -> Ribos lexer token stream
  -> Pegen-generated C parser
  -> generic CST 또는 typed AST builder
```

Generated C source는 `build/generated/ribos/parser.c`에 둔다. Generated source를
repository authority로 취급하지 않고 `.gram`, `Tokens`, generator revision과
generation receipt에서 재생성한다.

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

## G3: Standalone fixed parser backend

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
10. effect, capability와 worst-path budget

## G5: Parser acceptance gate

Parser slice의 acceptance는 다음을 모두 요구한다.

```text
Pegen grammar validation pass
deterministic C source generation
generated host C parser compile
positive corpus parse pass
negative corpus stable rejection pass
formatter parse-format-parse equality
repository documentation lint pass
Sphinx warnings-as-errors pass
```

이 gate는 다음을 주장하지 않는다.

- standalone no-heap firmware parser
- typed AST completion
- bytecode generation
- static verifier completion
- policy VM execution
- Ribon boot product linkage
- QEMU 또는 physical hardware policy execution

## 첫 구현 slice

첫 parser 구현 slice는 G0, G1과 G2를 함께 수행할 수 있다. Accepted EBNF가 Pegen이
처리할 수 있는 PEG construct로 구성되어 있고 동일한 surface의 축소 grammar가 C
source를 생성한 실험 경로를 갖기 때문이다.

첫 slice의 결과는 다음으로 제한한다.

```text
tracked:
  language/grammar/ribos.gram
  language/grammar/Tokens
  language/grammar/README.md
  tests/language/syntax/**
  generator wrapper와 receipt schema

generated:
  build/generated/ribos/parser.c
  build/results/ribos-parser-generation.json
```

Standalone fixed-allocation C parser는 G3의 별도 acceptance를 요구한다. Host C parser
생성 성공을 firmware-ready parser로 표현하지 않는다.
