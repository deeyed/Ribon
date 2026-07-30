---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-07-31
code_paths:
  - language/ribos/vm/
  - language/ribos/artifact/include/ribos/artifact/format.h
  - Makefile
tests:
  - make check-ribos-verifier
  - sanitizer verifier corpus
  - make check
  - make docs
hardware:
  - not-run
supersedes:
  - none
---

# Ribos bytecode verifier Stage-1 구현 기록

## 구현

`language/ribos/vm`에 compiler를 링크하지 않는 independent verifier를 추가했다.
Verifier는 artifact structural view와 selected product schema만 소비하고 caller-owned
workspace에서 다음을 재도출한다.

- type, aggregate와 constant table
- slot, frame와 source-map reference
- instruction chain과 direct branch/call target
- reachable CFG, bounded loop shape와 terminal mask
- per-block definite slot initialization
- opcode operand/result type와 helper schema signature
- recursive-call rejection, maximum call depth와 stack byte

Artifact format header에는 Policy IR enum과 독립적인 type, storage, shape, constant,
checked operator, function/slot/block flag와 terminal wire registry를 명시했다.
Selected schema identity도 canonical encoder byte를 SHA-256에 streaming하여 verifier
호출 경로에서 intermediate heap buffer를 제거했다.

Standalone host inspection command는 다음과 같다.

```sh
build/tools/ribos-verify POLICY.rba
```

## Host verification

Positive corpus는 semantic-positive `.rbs` 5개를 compiler로 `.rba`에 내린 뒤 standalone
verifier로 검사한다. Hostile corpus는 payload를 변조하고 SHA-256을 다시 봉인하여
단순 hash mismatch가 아닌 semantic rejection을 확인한다.

Hostile mutation 범위는 다음을 포함한다.

- unknown opcode
- header와 section bounds
- instruction chain boundary와 non-terminal block end
- branch와 direct-call target
- uninitialized same-typed slot
- frame offset와 stack metadata
- branch operand와 result slot type
- user struct member ordinal
- constant index와 stable hash
- product schema digest

Focused marker는 다음과 같다.

- `RIBOS-VERIFIER-CORPUS-OK positive=5 hostile=16 compiler-trusted=0 workspace=caller-owned stage=1`

## Evidence boundary

이 기록은 host Stage-1 verifier와 hostile mutation corpus, repository gate와 docs build
증거다. Signature/key policy, rollback, exact instruction/helper resource verifier, VM
dispatch, QEMU와 physical hardware 실행은 이 기록의 증거가 아니다.
