---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/examples/
  - language/ribos/frontend/tests/fragments/
  - language/ribos/examples/tests/executable_corpus_tests.py
tests:
  - make check-ribos-executable-corpus
  - qstar test --suite //tests:ribos_executable_corpus_tests
hardware:
  - none
supersedes:
  - parser-only positive Ribos example classification
---

# ADR: 공개 Ribos 예제는 terminal VM outcome까지 실행한다

## 맥락

초기 parser pilot의 `frontend/tests/positive/` 여섯 파일은 문법 표면을 넓게 확인하기 위한
조각이었다. 일부는 구형 `BootResult` 이름, 아직 의미가 없는 표면 helper 또는 policy entry가
아닌 반환 계약을 사용했다. 따라서 parser acceptance를 Ribos compiler나 VM 실행 가능성으로
오해할 수 있었고, 문서의 코드와 실제 source 사이에도 자동 drift gate가 없었다.

## 결정

1. 공개 positive corpus를 `language/ribos/examples/executable/`에 둔다.
2. 공개 예제의 entry path는 parse, semantic, Policy IR, resource closure, artifact,
   independent verifier와 production target-core VM host replay를 전부 통과한다.
3. 기존 parser-only 파일은 `frontend/tests/fragments/`로 옮기고 executable 또는 positive라고
   부르지 않는다.
4. Versioned JSON manifest가 source/artifact digest, exact resource closure와 terminal outcome을
   고정한다.
5. Normative contract의 tagged Markdown block을 source와 byte-for-byte 비교한다.
6. Parser와 semantic negative corpus는 기존 stable failure stage와 error code를 유지한다.

## 결과

문서 예제가 compiler evolution과 분리되어 썩는 것을 즉시 탐지할 수 있다. Resource 증가와
capability 확대도 reviewed manifest diff로 드러난다. 반면 fragment는 완결 정책이라는 주장을
하지 않으므로 parser 문법 회귀를 위한 넓은 표면을 계속 보존할 수 있다.

이 결정은 host replay까지만 다룬다. Production signature, rollback authority, firmware product
integration과 guest 또는 physical execution은 별도 계약과 증거를 요구한다.

## 기각한 대안

- 모든 parser fragment를 임의 builtin이나 compatibility alias로 통과시키는 방식은 language
  contract를 example에 맞춰 왜곡하므로 기각한다.
- 문서에서 source file만 link하고 코드 block을 제거하는 방식은 readable normative example과
  Markdown extraction gate라는 목표를 충족하지 않으므로 기각한다.
- Digest 없이 compile success만 검사하는 방식은 resource, artifact와 documentation drift를
  review surface로 만들지 못하므로 기각한다.
