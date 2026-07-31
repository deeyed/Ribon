---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-07-31
code_paths:
  - language/ribos/examples/
  - language/ribos/frontend/tests/fragments/
  - language/ribos/examples/tests/executable_corpus_tests.py
tests:
  - make check-ribos-executable-corpus
  - qstar test --suite //tests:ribos_executable_corpus_tests
  - make check-ribos-vm
  - make qstar-check
  - make docs
hardware:
  - not-run
supersedes:
  - none
---

# R02 Ribos executable corpus 구현 기록

## 구현

R02는 parser pilot의 여섯 syntax 파일을 audit해 parser-only fragment로 명시적으로
재분류했다. 별도 public corpus에는 collection, expression/control flow, 최소 recovery,
policy rejection, Result/Option과 typed declaration을 다루는 여섯 완결 정책을 추가했다.

Manifest는 각 source와 deterministic artifact digest, declared/reachable capability,
instruction/helper/stack/call-depth closure와 expected terminal outcome을 고정한다. Aggregate
gate는 parse와 semantic analysis, deterministic Policy IR/resource dump, artifact 재생성,
standalone verifier 및 네 번의 production VM host replay를 수행한다. Normative Sphinx
Markdown의 tagged code block도 해당 source와 byte-for-byte 일치해야 한다.

## 검증 결과

- Parser taxonomy:
  `RIBOS-PARSER-CORPUS-OK executable=6 fragments=6 negative=12`
- Stable semantic corpus:
  `RIBOS-SEMANTIC-CORPUS-OK positive=5 negative=18 deterministic-dump=1`
- Public executable closure:
  `RIBOS-EXECUTABLE-CORPUS-OK examples=6 docs-drift=0 parse=ok semantic=ok`
  `ir=deterministic artifact=deterministic verifier=independent vm=terminal repeats=4`
- VM aggregate:
  `RIBOS-VM-R16-AGGREGATE-OK core=production replay=deterministic`
  `conformance=24-opcodes hostile=bounded executable-examples=6 evidence=host-only`
- QStar suite: `status ok run=1 skip=0 fail=0`
- Object graph: `RIBOS-OBJECT-GRAPH-OK vm-ribon-imports=0 adapter=isolated`
- Documentation quality lint와 Sphinx `-W --keep-going` build: 성공

Manifest의 terminal outcome은 다섯 `boot-action`과 한 `policy-error`다. Public corpus의
semantic, artifact와 independent verifier 실패 수는 모두 0이고 tagged documentation
block drift도 0이다.

## 증거 경계

이 라운드의 실행 결과는 host-only다. `ribos-run`이 production target-core VM archive를
사용하지만 firmware product, QEMU guest와 physical board에서 예제를 실행한 것은 아니다.
Unsigned development artifact를 사용하므로 production Ed25519 key policy와 rollback counter도
증명하지 않는다.
