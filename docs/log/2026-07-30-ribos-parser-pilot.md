---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-07-30
code_paths:
  - language/grammar/
  - language/generated/
  - language/include/ribos/parser.h
  - language/src/
  - language/tools/ribos_parse.c
  - tools/ribosc/
  - tests/language/
tests:
  - make check-ribos-parser-pilot
  - make ribos-parser-regenerate-check
  - make check
  - make docs
hardware:
  - not-run
supersedes:
  - none
---

# Ribos Pegen parser pilot 기록

## 구현 범위

- Accepted Ribos EBNF의 declaration, statement, expression, pattern, bounded type,
  collection, decorator와 postfix 표면을 Pegen grammar와 token specification으로
  내렸다.
- CPython Pegen C generator가 만든 parser와 token number header를 repository에
  operational snapshot으로 추적했다.
- UTF-8, comment, logical newline, integer, string, identifier와 punctuator를 처리하는
  Ribos lexer를 구현했다.
- Pegen skeleton이 요구하는 token, keyword, generic sequence와 recursion hook을
  standalone host C runtime으로 제공했다.
- Syntax-only public API와 단일 source pilot CLI를 추가했다.
- Positive 6개와 negative 12개 fixture로 full boot policy, 전체 operator surface,
  declaration, control flow, collection과 금지 syntax를 검사했다.

## 생성 증거

사용한 CPython checkout revision은 다음과 같다.

```text
9ccd5bb81edde823fb5fdbd51287f4a0ddfcd149
```

명시적 regeneration comparison에서 tracked C와 token header는 모두
`unchanged`였으며 다음 marker가 출력됐다.

```text
RIBOS-PEGEN-SNAPSHOT-OK
grammar=491d95b76b1de8af9e35fd0de769b0f3622473ec3a3dc9d5fee5bcb820bc1c33
tokens=e165aaa43d399dfde9ba9744fac94cc836f3665ccf56777b0d9757ce963f85fc
pegen=9ccd5bb81edde823fb5fdbd51287f4a0ddfcd149
```

Generated artifact SHA-256은 다음과 같다.

```text
5b37bd2d40376c114aa601dc7c1919d86fa4e436f9ea6daea75587e6a20e19c6  ribos_parser.c
57e2b958f39261d9f2c0be9d1f915c86c1442356ce624c6abb45e739666a17d1  ribos_tokens.h
```

## Parser pilot 결과

Apple Clang 21.0.0, Python 3.14.6 host 환경에서 generated C를
`-std=c11 -Wall -Wextra -Werror`로 컴파일했다. Corpus gate 결과는 다음과 같다.

```text
RIBOS-PARSER-CORPUS-OK positive=6 negative=12
```

Full policy fixture는 다음 syntax-only summary를 반환했다.

```text
RIBOS-PARSER-PILOT-OK
bytes=1420
tokens=321
declarations=3
depth=60
```

`RIBOS_PEGEN_ROOT`를 존재하지 않는 path로 지정한 강제 재빌드에서도
`check-ribos-parser-pilot`이 성공했다. 따라서 일반 parser compile, snapshot digest
검사와 corpus 실행 경로는 Pegen checkout을 읽거나 generator를 실행하지 않았다.

AddressSanitizer와 UndefinedBehaviorSanitizer를 함께 사용한 corpus 실행도 같은
`positive=6 negative=12` marker로 종료했다. 이 host의 AddressSanitizer는 leak
detection을 지원하지 않아 `detect_leaks=0`으로 실행했으며 leak 증거로 사용하지
않는다.

Repository aggregate와 문서 gate는 각각 다음 terminal marker로 종료했다.

```text
RIBON-R5-AGGREGATE-OK
Sphinx build succeeded
```

## 증거 경계

이 기록의 evidence class는 host compile과 syntax corpus다. Parser는 generic syntax
sequence만 만들며 typed AST를 만들지 않는다. 다음 항목은 이 pilot이 증명하지 않는다.

- name resolution, type inference와 mutation checking
- bounded collection capacity와 loop bound 검증
- capability, effect, typestate와 terminal action 검증
- formatter와 parse-format-parse equality
- policy IR, bytecode, verifier와 VM 실행
- no-heap fixed-capacity firmware parser
- Ribon boot product linkage
- QEMU, firmware 또는 physical board policy execution
