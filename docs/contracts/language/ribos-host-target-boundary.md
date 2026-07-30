---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/base/
  - language/ribos/host/
  - language/ribos/frontend/
  - language/ribos/ir/
  - language/ribos/artifact/
  - language/ribos/vm/
  - Makefile
tests:
  - make ribos-libraries
  - make check-ribos-host-boundary
  - make check-ribos-parser-pilot
  - make check-ribos-verifier
  - make check
  - make docs
hardware:
  - none
supersedes:
  - implicit Ribos host and target build boundary
---

# Ribos host와 target 경계 계약

## 목적

이 계약은 Ribos compiler toolchain과 향후 Ribon target VM이 공유할 수 있는
architecture-neutral 부분을 source directory와 static archive 수준에서 분리한다.
Source language, Policy IR, bytecode ISA와 verifier 의미는 다른 계약이 소유한다.

## 계층과 archive

| 소유 계층 | 허용된 책임 | 생성 archive |
| --- | --- | --- |
| `base` | allocator/writer descriptor와 checked size/offset 산술 | target core |
| `schema` | canonical product schema와 identity | target core |
| `artifact` | wire registry, reader, codec와 hash | target core |
| `vm` | caller-workspace verifier, preparation와 bounded runtime storage | target core |
| `host` support | libc allocator, `FILE` writer와 hosted format | host support |
| `frontend` + `ir` | source compile, typed AST, IR와 resource closure | host compiler |
| `host` emitter | validated Policy IR에서 `.rba` emission | host compiler |
| `host` tools | parser/verifier CLI와 Pegen orchestration | executable only |

정상 산출물 이름은 다음과 같다.

```text
build/ribos/libribos-target-core.a
build/ribos/libribos-host-support.a
build/ribos/libribos-host-compiler.a
```

Host CLI의 링크 순서는 `host compiler -> target core -> host support`다. Target product는
host support와 host compiler를 링크할 수 없다.

## Neutral allocator 계약

`RibosAllocator`는 allocate, resize와 deallocate callback 및 opaque context를 가진다.

- size와 alignment는 byte 단위다.
- alignment는 nonzero power of two여야 한다.
- zero-size allocation은 `NULL`이다.
- zeroed allocation은 `count * element_size` overflow를 먼저 검사한다.
- object를 생성한 allocator가 resize와 release authority를 계속 소유한다.
- release의 size와 alignment는 backend accounting용이며 host adapter는 이를 무시할
  수 있다.

Frontend parser, AST arena, semantic lowering, Policy IR module과 resource closure는
직접 `malloc`, `calloc`, `realloc` 또는 `free`를 호출할 수 없다. Public compile API는
allocator를 explicit argument로 받는다. IR module과 compiler에 서로 다른 allocator
descriptor를 전달하면 compile은 invalid argument로 실패한다.

## Neutral writer 계약

`RibosWriter`는 opaque context와 `va_list` format callback을 가진다. Frontend와 IR
diagnostic dump는 `FILE`, `fprintf`, `fputc`, `fputs` 또는 `snprintf`를 직접 사용할 수
없다. Hosted `FILE`과 C formatting은 `language/ribos/host` adapter만 소유한다.

Writer는 debug/inspection sink다. Artifact wire format이나 VM helper ABI가 아니며
production target이 writer를 반드시 제공할 필요는 없다.

## Neutral checked 산술

`Ribos base`의 checked API는 process-local `size_t`와 architecture-neutral `uint64_t`
산술을 분리한다. Runtime plan, artifact range와 caller workspace는 unchecked
`count * stride`, `offset + length` 또는 alignment round-up을 사용할 수 없다. 이
helper는 allocation을 수행하거나 product별 cap을 소유하지 않는다.

## Pegen과 CLI

다음은 host-only 경로다.

```text
language/ribos/host/pegen/
language/ribos/host/tools/parse.c
language/ribos/host/tools/verify.c
```

Pegen은 grammar 변경이나 generator 오류 조사 시 explicit target에서만 실행한다.
정상 parser, compiler, verifier와 aggregate build는
`frontend/generated/parser.c`의 tracked snapshot만 사용한다.

## Target core 금지 표면

Target-safe source와 `libribos-target-core.a`에는 다음이 없어야 한다.

- `<stdio.h>`, `<stdlib.h>`와 `FILE`
- hosted allocation과 stream/file function
- frontend, Pegen, Policy IR 또는 host header
- artifact emitter
- architecture, board, OS 또는 Ribon product-specific dispatch

Target core는 freestanding C로 compile되어야 한다. `memcpy`, `memset`처럼 platform이
제공할 수 있는 freestanding memory primitive는 허용한다.

## Gate와 증거 한계

```sh
make ribos-libraries
make check-ribos-host-boundary
```

Boundary gate는 다음을 검사한다.

1. target-safe source의 금지 include와 hosted call
2. 이전 host tool/emitter path가 남지 않았는지
3. target/host archive member allowlist
4. target archive의 hosted undefined symbol
5. counting allocator의 allocation/release size와 zero-live-object closure

이 성공은 compiler와 target verifier/runtime storage가 분리된 architecture-neutral
object graph임을 증명한다. VM opcode interpreter, helper dispatch, signature trust,
boot integration, QEMU와 physical hardware 실행은 증명하지 않는다.
