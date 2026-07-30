# Ribos language project

이 디렉터리는 Ribos source compiler, Policy IR, product schema와 향후 VM backend를
서로 독립된 project boundary로 유지한다. 공식 source 확장자는 `.rbs`다. `.ribos`
확장자와 호환 alias는 없다.

```text
language/ribos/
  README.md
  frontend/
    README.md
    grammar/
    generated/
    include/ribos/frontend/
    src/
    tools/
    tests/
  schema/
    README.md
    include/ribos/schema/
    src/
    tests/
  ir/
    README.md
    include/ribos/ir/
    src/
    tests/
  artifact/
    README.md
    include/ribos/artifact/
    src/
    tests/
  vm/
    README.md
```

경계는 다음과 같다.

| 계층 | 입력 | 출력 | 금지된 의존 |
| --- | --- | --- | --- |
| `frontend` | UTF-8 `.rbs`, product schema | typed AST, Policy IR v1 | VM bytecode와 runtime state |
| `schema` | immutable product/plugin descriptor | canonical schema bytes와 SHA-256 identity | parser AST와 VM opcode |
| `ir` | resolved type, CFG, helper stable ID | typed module과 resource closure | Pegen, source token과 product C pointer |
| `artifact` | validated Policy IR와 resource closure | canonical `.rba`, borrowed structural view | frontend AST, packed C wire image와 VM dispatch |
| `vm` | 향후 verified executable artifact | bounded policy execution | `.rbs`, Pegen과 frontend AST |

`frontend/src/lower.c`는 frontend-private AST를 public `ribos/ir` builder API로
변환하는 bridge다. IR module, validator, CFG/resource analyzer와 deterministic
dump는 `ir/`가 소유한다.
Artifact emitter는 IR public borrowed view와 resource analyzer만 소비하며 frontend
private header를 include할 수 없다. VM은 artifact reader와 독립 verifier를 통해서만
executable view를 얻는다.

Product schema는 type, fact/member, helper signature/capability와 typed handoff field를
소유한다. Reference schema는 `schema/src/schema.c`에 있지만, 장기 product build는
동일한 versioned artifact를 product/plugin graph에서 생성해 compiler와 artifact
verifier에 함께 전달한다. Pointer나 C structure padding은 schema identity에
포함되지 않는다.

Parser grammar의 machine-readable 정본은 다음 두 파일이다.

```text
language/ribos/frontend/grammar/parser.gram
language/ribos/frontend/grammar/Tokens
```

일반 build는 Pegen을 실행하지 않고 tracked C snapshot을 컴파일한다.

```text
language/ribos/frontend/generated/parser.c
language/ribos/frontend/generated/parser.receipt.json
language/ribos/frontend/generated/tokens.h
```

문법이나 generator integration을 의도적으로 바꿀 때만 parser를 재생성한다.

```sh
make ribos-parser-generate \
    RIBOS_PEGEN_ROOT=/path/to/cpython/Tools/peg_generator
make ribos-parser-regenerate-check \
    RIBOS_PEGEN_ROOT=/path/to/cpython/Tools/peg_generator
```

고정한 CPython Pegen revision은
`9ccd5bb81edde823fb5fdbd51287f4a0ddfcd149`다. Generated parser는 CPython AST나
Python object를 생성하지 않고 standalone bounded Ribos AST runtime에 연결된다.

Host gates는 서로 다른 증거를 제공한다.

```sh
make check-ribos-parser-pilot
make check-ribos-semantics
make check-ribos-schema
make check-ribos-ir
make check-ribos-resources
make check-ribos-artifact
```

- parser gate는 `.rbs` syntax acceptance/rejection만 증명한다.
- semantic gate는 type, mutation, closed match, capability와 source helper bound를
  증명한다.
- schema gate는 descriptor validation, canonical encoding과 reference identity를
  증명한다.
- IR gate는 explicit CFG, typed virtual slot, direct call/branch, source map,
  aggregate shape와 helper call-site table을 증명한다.
- resource gate는 reachable CFG, terminal closure, loop/call/instruction/helper bound와
  VM 독립 type/slot/frame layout을 증명한다.
- artifact gate는 VM ABI 1.0/ISA 1.0 little-endian serialization, payload SHA-256,
  canonical section range, optional source map과 deterministic emission을 증명한다.

CLI는 host inspection을 위해 다음 mode를 제공한다.

```sh
build/tools/ribos-parse --check policy.rbs
build/tools/ribos-parse --dump-tokens policy.rbs
build/tools/ribos-parse --dump-ast policy.rbs
build/tools/ribos-parse --dump-semantics policy.rbs
build/tools/ribos-parse --dump-ir policy.rbs
build/tools/ribos-parse --dump-resources policy.rbs
build/tools/ribos-parse --emit-artifact policy.rba policy.rbs
```

Host compiler와 IR module은 hard capacity를 가진 heap-backed 도구다. Artifact
structural reader는 allocation하지 않지만 아직 hostile bytecode semantic verifier나
Ed25519 trust verifier는 아니다. 이 구현은 firmware VM, verified bytecode, no-heap
runtime parser 또는 Ribon boot product 안의 policy 실행을 증명하지 않는다.
Production boot product는 `.rbs`, Pegen과 host compiler를 링크하지 않는다.

Generated C source는 Python Software Foundation License Version 2로 제공된 CPython
Pegen generator를 사용해 만들었다. Pegen source는 수정하지 않았다.
