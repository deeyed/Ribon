# Ribos language project

이 디렉터리는 Ribos source compiler, Policy IR, product schema와 VM backend를
서로 독립된 project boundary로 유지한다. 공식 source 확장자는 `.rbs`다. `.ribos`
확장자와 호환 alias는 없다.

```text
language/ribos/
  README.md
  base/
    include/ribos/base/
    src/
  host/
    include/ribos/host/
    pegen/
    src/
    tests/
    tools/
  frontend/
    README.md
    grammar/
    generated/
    include/ribos/frontend/
    src/
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
    include/ribos/vm/
    src/
    tests/
```

경계는 다음과 같다.

| 계층 | 입력 | 출력 | 금지된 의존 |
| --- | --- | --- | --- |
| `base` | allocator/writer callback | target-neutral service descriptor | libc, platform과 product |
| `host` | C hosted runtime, validated IR | compiler adapters, emitter와 CLI | target dispatch와 boot product |
| `frontend` | UTF-8 `.rbs`, product schema | typed AST, Policy IR v1 | VM bytecode와 runtime state |
| `schema` | immutable product/plugin descriptor | canonical schema bytes와 SHA-256 identity | parser AST와 VM opcode |
| `ir` | resolved type, CFG, helper stable ID | typed module과 resource closure | Pegen, source token과 product C pointer |
| `artifact` | validated Policy IR와 resource closure | canonical `.rba`, borrowed structural view | frontend AST, packed C wire image와 VM dispatch |
| `vm` | untrusted `.rba`, selected product schema | two-stage verification, prepared program과 bounded execution | `.rbs`, Pegen, frontend AST와 Policy IR |

`frontend/src/lower.c`는 frontend-private AST를 public `ribos/ir` builder API로
변환하는 bridge다. IR module, validator, CFG/resource analyzer와 deterministic
dump는 `ir/`가 소유한다.
Artifact emitter는 IR public borrowed view와 resource analyzer만 소비하며 frontend
private header를 include할 수 없다. VM은 artifact reader와 독립 verifier를 통해서만
executable view를 얻는다.

Object graph는 다음 세 archive로 hard cut한다.

```text
libribos-target-core.a
  = base + schema + artifact reader/codec/hash + independent verifier
    + PreparedProgram + storage + interpreter + handle/helper/terminal runtime

libribos-host-support.a
  = libc allocator + FILE writer + hosted format adapter

libribos-host-compiler.a
  = frontend + Policy IR/resource closure + artifact emitter
```

Frontend와 IR은 직접 hosted allocation 또는 `FILE`을 사용하지 않고 explicit
`RibosAllocator`와 `RibosWriter` authority를 받는다. Target product는 host support와
host compiler archive를 링크할 수 없다.

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

Pegen integration과 host CLI의 소유 경로는 다음과 같다.

```text
language/ribos/host/pegen/
language/ribos/host/tools/
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
make check-ribos-verifier
make check-ribos-host-boundary
make check-ribos-host-tools
make check-ribos-replay
make check-ribos-conformance
make check-ribos-hostile
make check-ribos-vm
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
- verifier gate는 compiler를 링크하지 않고 Stage-1 structural/type/CFG/frame과
  Stage-2 capability/ownership/typestate/resource/terminal closure를 artifact
  byte에서 재도출하고 hostile mutation을 거부함을 증명한다.
- host-boundary gate는 freestanding target archive에서 hosted allocation/I/O,
  frontend, Policy IR와 emitter dependency가 제거되었음을 증명한다.
- host replay gate는 같은 target-core VM을 `ribos-run`에 연결하고 deterministic
  input tuple, ISA 24 opcode, cross-layer resource closure와 bounded hostile input을
  증명한다.

CLI는 host inspection을 위해 다음 mode를 제공한다.

```sh
build/tools/ribosc --check policy.rbs
build/tools/ribosc --dump-tokens policy.rbs
build/tools/ribosc --dump-ast policy.rbs
build/tools/ribosc --dump-semantics policy.rbs
build/tools/ribosc --dump-ir policy.rbs
build/tools/ribosc --dump-resources policy.rbs
build/tools/ribosc --emit-artifact policy.rba policy.rbs
build/tools/ribos-verify policy.rba
build/tools/ribos-run \
    --context context.rbctx \
    --transcript helpers.rbtr \
    policy.rba
```

Host compiler와 IR module은 caller-supplied allocator와 hard capacity를 가진 hosted
도구다. Artifact
structural reader와 two-stage verifier는 allocation하지 않는다. Stage-2 verifier는
caller-owned workspace에서 exact instruction/helper resource bound까지 검사하지만
Ed25519 trust와 rollback을 증명하지 않는다. `ribos-run`은 target-core production
VM을 host에서 실행하지만 firmware VM integration, Ribon boot product 안의 policy
dispatch, QEMU 또는 hardware 실행을 증명하지 않는다.
Production boot product는 `.rbs`, Pegen과 host compiler를 링크하지 않는다.

Generated C source는 Python Software Foundation License Version 2로 제공된 CPython
Pegen generator를 사용해 만들었다. Pegen source는 수정하지 않았다.
