---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-30
code_paths:
  - language/grammar/
  - language/generated/
  - language/include/
  - language/src/
  - tools/ribosc/
  - tests/language/
tests:
  - make check-ribos-parser-snapshot
  - make check-ribos-parser-pilot
  - make ribos-parser-regenerate-check
hardware:
  - none
supersedes:
  - none
---

# ADR 0016: 추적되는 Ribos Pegen parser snapshot

## Context

Ribos source grammar는 Pegen 형식을 machine-readable 정본으로 사용한다. 그러나 모든
Ribon build에서 Pegen을 실행하면 다음 문제가 생긴다.

- 일반 developer와 CI build가 CPython source checkout과 Python generator 환경에
  종속된다.
- Pegen revision 차이가 C source drift를 만들 수 있다.
- 문법을 바꾸지 않은 bootloader 작업도 parser generator를 실행한다.
- 생성기 실패와 tracked C parser의 compile 실패를 같은 failure domain에 넣는다.
- source parser를 사용하지 않는 Ribon boot product까지 host compiler dependency를
  상속할 수 있다.

반대로 generated C를 추적만 하고 grammar와의 관계를 검사하지 않으면 stale parser가
정본 문법과 다르게 동작할 수 있다.

## Decision

Ribos parser는 authority와 operational snapshot을 분리한다.

```text
normative syntax authority:
  docs/contracts/language/ribos-source-language.md
  language/grammar/ribos.gram
  language/grammar/Tokens

tracked operational snapshot:
  language/generated/ribos_parser.c
  language/generated/ribos_parser.receipt.json
  language/generated/ribos_tokens.h
```

일반 build와 `make check`는 tracked snapshot을 직접 컴파일한다. Pegen module을
import하거나 실행하지 않는다. Grammar와 token specification의 SHA-256은 generated
snapshot header에 기록하며 `check_parser_snapshot.py`가 두 digest를 비교한다. 이
staleness gate는 Pegen checkout 없이 실행 가능해야 한다.
Generation receipt는 input과 generated output digest, pinned revision, canonical
generator command와 exit status를 기록한다.

Parser 재생성은 다음 두 explicit target으로만 수행한다.

```text
make ribos-parser-generate RIBOS_PEGEN_ROOT=<pinned-root>
make ribos-parser-regenerate-check RIBOS_PEGEN_ROOT=<pinned-root>
```

Generator wrapper는 CPython checkout의 license file과 exact Git revision
`9ccd5bb81edde823fb5fdbd51287f4a0ddfcd149`를 확인한다. 다른 revision에서는
snapshot을 갱신하지 않는다. Grammar, Tokens 또는 generator integration을 바꾸는
commit은 generated C와 token header를 같은 commit에서 갱신하고 explicit
regeneration comparison을 통과해야 한다.

Tracked C parser는 Pegen C skeleton을 사용하지만 CPython tokenizer, AST, `PyObject`,
reference count와 exception runtime을 링크하지 않는다. Host pilot runtime은 Ribos
lexer, token span, generic sequence와 farthest-progress diagnostic을 자체 제공한다.
이 runtime의 dynamic allocation은 host compiler pilot에만 허용된다.

Production boot product는 이 source parser를 자동으로 링크하지 않는다. Firmware가
source parsing을 선택하는 경우 fixed token, AST, memo와 diagnostic capacity를 가진
별도 backend acceptance를 통과해야 한다.

## Consequences

- 문법 변경이 없는 일반 Ribon build는 CPython checkout 없이 재현할 수 있다.
- Generated parser code review와 compiler warning gate가 repository 안에서
  이루어진다.
- Grammar digest mismatch는 generation toolchain 부재와 무관하게 즉시 실패한다.
- Generator revision 변경은 의도적인 ADR 또는 dependency review 대상이 된다.
- Repository에는 큰 generated C translation unit이 남지만, source authority가
  generated file로 이동하지는 않는다.
- Host syntax acceptance는 no-heap firmware parser, typed AST, type checker, verifier,
  bytecode 또는 VM 완료를 뜻하지 않는다.

## 기각한 대안

### 모든 build에서 Pegen 실행

문법 변경과 무관한 build에 external generator dependency를 강제하고 failure domain을
넓히므로 채택하지 않는다.

### Generated C를 추적하지 않음

일반 build가 Pegen checkout을 요구하거나 release source archive에 parser source가
없게 되므로 채택하지 않는다.

### Generated C를 정본으로 취급

사람이 검토하는 language contract와 Pegen grammar보다 generator output이 우선하게
되므로 채택하지 않는다. Generated file은 수정 대상이 아니라 재생성 대상이다.

### CPython parser runtime 직접 링크

Ribos가 Python object와 exception 의미를 상속하고 boot product TCB가 커지므로
채택하지 않는다.
