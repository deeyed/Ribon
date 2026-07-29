# Ribos language project

이 디렉터리는 Ribos source language의 grammar, generated snapshot, host parser,
developer tool과 syntax corpus를 하나의 프로젝트 경계로 묶는다.

```text
language/ribos/
  README.md
  grammar/
    parser.gram
    Tokens
  generated/
    parser.c
    parser.receipt.json
    tokens.h
  include/ribos/
    parser.h
  src/
    lexer.c
    parser.c
    parser_internal.h
    runtime.c
  tools/
    check_parser_snapshot.py
    generate_parser.py
    parse.c
  tests/
    parser_pilot_tests.py
    positive/
    negative/
```

`grammar/parser.gram`과 `grammar/Tokens`는 Ribos source syntax의 machine-readable
정본이다. `docs/contracts/language/ribos-source-language.md`의 EBNF와 같은 변경에서
동기화한다.

`language/ribos/`가 이미 project namespace를 제공하므로 내부 C와 generated file은
`ribos_` filename prefix를 반복하지 않는다. 다만 public C symbol과 installed include
namespace는 충돌 방지를 위해 `ribos_`와 `ribos/`를 유지한다.

일반 Ribon build는 Pegen을 실행하지 않는다. 다음 snapshot을 그대로 컴파일한다.

```text
language/ribos/generated/parser.c
language/ribos/generated/parser.receipt.json
language/ribos/generated/tokens.h
```

문법이나 generator integration을 의도적으로 바꿀 때만 parser를 재생성한다.

```sh
make ribos-parser-generate \
    RIBOS_PEGEN_ROOT=/path/to/cpython/Tools/peg_generator
```

재생성 결과가 tracked snapshot과 같은지 확인하려면 다음 explicit gate를 사용한다.

```sh
make ribos-parser-regenerate-check \
    RIBOS_PEGEN_ROOT=/path/to/cpython/Tools/peg_generator
```

Generator 기준 revision은
`9ccd5bb81edde823fb5fdbd51287f4a0ddfcd149`다. Wrapper는 CPython repository의 exact
revision과 PSF license 파일을 확인하고 generation receipt에 digest를 기록한다.

Generated parser는 host pilot lane이며 CPython AST나 Python object를 만들지 않는다.
Pegen C skeleton의 operation은 `language/ribos/src/parser_internal.h`가 제공하는
standalone host runtime에 연결한다.

Generated C source는 Python Software Foundation License Version 2로 제공된 CPython
Pegen generator를 사용해 만들었다. Pegen source를 수정하지 않았고 Ribon 전용
grammar, token set, header, trailer와 runtime을 추가했다. CPython Pegen이나 generated
host parser가 production Ribon boot product에 자동으로 링크되지는 않는다.
