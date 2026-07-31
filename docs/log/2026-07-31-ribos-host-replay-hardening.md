---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-07-31
code_paths:
  - language/ribos/host/tools/run.c
  - language/ribos/host/tests/
  - language/ribos/vm/tests/opcode_conformance.rbs
  - Makefile
tests:
  - make check-ribos-host-tools
  - make check-ribos-replay
  - make check-ribos-conformance
  - make check-ribos-hostile
  - make check-ribos-vm
  - make check-ribos-host-boundary
  - make check
  - make docs
hardware:
  - not-run
supersedes:
  - none
---

# Ribos production VM host replay와 hardening 구현 기록

## 구현

Host compiler binary 이름을 정본 표기 `ribosc`로 hard cut하고 `ribos-verify`,
`ribos-run`과 함께 build target으로 묶었다. `ribos-run`은 compiler archive가 아니라
production `libribos-target-core.a`를 링크한다.

Context snapshot과 helper transcript는 fixed header/row, checked range, zero reserved,
SHA-256 binding을 가진 little-endian format으로 구현했다. Hosted embedder는 artifact의
실제 helper import와 product schema에서 execution contract를 만들고 transcript로
result, counter, duration, journal receipt와 handle object를 공급한다.

Canonical report에는 outcome, verifier/runtime resource, terminal/journal/fault,
recovery-once와 source 진단을 기록한다. 동일 입력은 네 번 반복해 byte-for-byte 같은
report를 요구한다.

## 검증

- source-to-run positive 7개와 negative 4개
- ISA v1 opcode 24개 전체
- compiler, verifier, runtime의 instruction/helper/stack/call-depth upper bound 일치
- source 위치만 바꾼 artifact의 semantic report 일치
- artifact/context/transcript mutation 72개
- truncation 15개
- valid hash를 다시 만든 invalid opcode와 wrong helper transcript
- partial journal callback fault의 deterministic `VmFault`와 recovery exactly once

Hostile corpus는 timeout과 sanitizer marker를 함께 검사한다. 별도 sanitizer build는
host-only evidence로 실행한다.

## 증거 한계

이 기록은 host end-to-end와 production VM core replay 증거다. Production signature,
rollback, Ribon product helper, OTA/network/storage side effect, QEMU와 physical
hardware는 실행하지 않았다.

Artifact codec은 source-map section omission을 format으로 표현하지만 현재 independent
verifier execution path는 section을 요구한다. 이 라운드는 verifier 구현 범위를
확장하지 않고 source 위치 이동으로 diagnostic-only 의미를 검증했다.
