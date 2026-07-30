---
doc_type: devlog
status: accepted
authority: historical
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
  - make check-ribos-host-boundary
  - make check-ribos-parser-pilot
  - make check-ribos-semantics
  - make check-ribos-schema
  - make check-ribos-ir
  - make check-ribos-resources
  - make check-ribos-artifact
  - make check-ribos-verifier
  - make check
  - make docs
hardware:
  - not-run
supersedes:
  - none
---

# Ribos host와 target 경계 hard cut 구현 기록

## 구현

Hosted adapter, tools와 generator orchestration을 `language/ribos/host/`로 이동했다.
Frontend와 Policy IR은 explicit `RibosAllocator`와 `RibosWriter`를 사용하며 직접
hosted allocation과 `FILE`에 의존하지 않는다.

Artifact emitter는 host compiler로 이동했고 target-safe archive에는 neutral base,
schema, artifact reader/codec/hash와 independent verifier만 남겼다. Build는 서로 다른
archive를 생성한다.

```text
libribos-target-core.a
libribos-host-support.a
libribos-host-compiler.a
```

Compatibility forwarding header, 이전 tool path와 old Make alias는 만들지 않았다.
Parser와 verifier CLI의 사용자-facing 이름과 accepted source/ISA 의미는 유지했다.

## 검증

`check-ribos-host-boundary`는 target source의 include/call scan, 이전 path 부재,
archive member와 undefined hosted symbol을 검사한다. Counting allocator test는 parser,
IR module과 resource closure가 정확한 allocation size로 모든 storage를 반환하는지
검사한다. Parser, semantic, IR, resource, artifact와 hostile verifier corpus는 분리된
archive link graph 위에서 다시 실행한다.

이 라운드의 증거는 host build와 contract boundary다. VM dispatch, Ribon boot product
linkage, QEMU 또는 physical hardware 실행은 수행하지 않았다.
