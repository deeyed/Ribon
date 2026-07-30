---
doc_type: devlog
status: historical
authority: informative
last_verified: 2026-07-31
code_paths:
  - language/ribos/schema/
  - language/ribos/vm/
tests:
  - make check-ribos-schema
  - make check-ribos-ir
  - make check-ribos-resources
  - make check-ribos-verifier
  - make check
  - make docs
hardware:
  - none
supersedes:
  - none
---

# Ribos bytecode verifier Stage-2 구현 기록

## 구현 범위

- Product schema format 1.1
  - policy context/action/error ABI
  - copy/affine/linear ownership
  - helper parameter borrow/consume
  - typestate transition
  - terminal boot action
- Compiler-independent Stage-2 verifier
  - reachable capability closure
  - exact instruction/helper path bound
  - helper stable-ID별 bound
  - opaque handle provenance
  - CFG ownership must-availability
  - terminal action count와 fail-closed error/TRAP
- Caller-owned allocation-free workspace
- Stage-2 host tool marker와 hostile corpus

## Hostile corpus

Stage-1 hostile mutation에 다음 Stage-2 사례를 추가했다.

- compiler/header capability metadata 동시 축소
- compiler/header instruction/helper upper bound 동시 위조
- helper-specific bound 위조
- `ImageId` symbol을 동일 크기 `Image` opaque handle로 재타이핑
- `VerifiedImage` 이중 consume
- terminal boot action 뒤 error return
- terminal action 없는 policy

Gate marker:

```text
RIBOS-VERIFIER-CORPUS-OK positive=5 hostile=24 compiler-trusted=0 workspace=caller-owned stage=2
```

Reference schema identity:

```text
RIBOS-SCHEMA-TEST-OK format=1.1 identity=237898e5b4b7fd5f8cccf9edcf5da50fb6699f24b869e878638f27885091a4a8
```

`policy_pipeline.rbs` pilot artifact를 standalone verifier에 넣은 결과는 다음과
같았다.

```text
RIBOS-VERIFIER-STAGE2-OK types=43 functions=2 blocks=16 instructions=74 entry-frame=328 entry-stack=352 call-depth=2 capabilities=0x00000063 instruction-upper=76 helper-upper=7 workspace=3000
```

같은 verifier corpus는 별도 build root의 AddressSanitizer와
UndefinedBehaviorSanitizer 조합에서도 통과했다. 전체 host 회귀는
`RIBON-R5-AGGREGATE-OK`, 문서는 warning-as-error Sphinx+Breathe build 성공으로
닫았다.

## 증거 경계

이 기록은 host compiler artifact와 allocation-free verifier unit/corpus 증거다.
VM dispatch, runtime counter, Ed25519 trust store, rollback, Ribon boot product linkage,
QEMU와 physical hardware 실행을 증명하지 않는다.
