---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-07-31
code_paths:
  - language/ribos/artifact/
  - language/ribos/host/tools/parse.c
  - language/ribos/host/src/artifact_emitter.c
  - language/ribos/ir/include/ribos/ir/ir.h
  - language/ribos/ir/src/module.c
  - Makefile
tests:
  - make check-ribos-artifact
  - sanitizer Ribos artifact corpus
  - make check
  - make docs
hardware:
  - not-run
supersedes:
  - none
---

# Ribos bytecode ISA와 artifact 구현 기록

## 구현

Validated Policy IR v1.1에서 VM ABI 1.0/bytecode ISA 1.0 `.rba`를 생성하는 deterministic
host emitter를 추가했다. Emitter는 module validation과 resource closure를 다시
실행하고 type, shape, constant, function, block, loop, slot, instruction, operand,
helper import/bound와 optional source-map table을 봉인한다.

Policy IR은 private storage를 노출하지 않고 `RibosIrModuleView` borrowed view를
artifact emitter에 제공한다. 이 view는 host API이며 wire ABI가 아니다.

Artifact codec은 explicit little-endian byte reader/writer와 checked
add/multiply/alignment를 사용한다. Allocation-free reader는 canonical envelope,
payload SHA-256, version, section directory, zero padding과 range를 검사한다.

Host CLI는 다음 pilot mode를 제공한다.

```sh
build/tools/ribos-parse --emit-artifact OUTPUT.rba SOURCE.rbs
```

일반 build는 Pegen을 다시 실행하지 않으며 tracked parser snapshot 위에서 compiler와
artifact emitter를 실행한다.

## Host verification

Structural C test는 다음을 검사한다.

- 두 번 emission한 artifact의 byte identity
- unsigned와 shaped Ed25519 envelope
- canonical 112-byte signature message
- optional source-map section
- output capacity failure
- payload hash mismatch
- overflowing payload, section offset/length와 directory length mutation rejection

Corpus test는 5개의 semantic-positive `.rbs`를 각각 두 번 compile하고 Python
`hashlib.sha256`와 independent little-endian reader로 envelope, section layout,
schema identity와 budget을 교차 검사한다.

관찰한 focused marker는 다음과 같다.

- `RIBOS-ARTIFACT-TEST-OK le=1 hash=sha256 signed-envelope=1 source-map=optional mutations=5`
- `RIBOS-ARTIFACT-CORPUS-OK fixtures=5 deterministic=1 endian=little hash=sha256 vm=1.0 isa=1.0`

## Evidence boundary

이 라운드의 증거는 host emitter, structural reader, deterministic corpus와 sanitizer,
repository gate 및 documentation build에 한정된다.

다음은 아직 이 기록이 주장하지 않는다.

- hostile bytecode의 type/CFG/helper semantic verifier
- Ed25519 cryptographic verification과 production key policy
- VM dispatch와 runtime budget counter
- Ribon boot product linkage
- QEMU 또는 physical hardware policy execution
