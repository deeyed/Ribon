---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-26
code_paths:
  - docs/
  - include/Ribon/
  - src/
  - tools/lint/documentation_quality_lint.py
tests:
  - ribon-documentation-quality-lint
  - ribon-docs
hardware:
  - none
supersedes:
  - none
---

# 문서 품질 Gate 계약

문서 품질 gate는 정본 문서가 checkout 보고서로 변하거나 공개 C API 주석 부채가
증가하는 것을 막는다.

## 적용 범위

- `docs/canonical/`
- `docs/contracts/`
- `docs/policy/`
- `docs/adr/`
- `include/Ribon/`
- `src/`

`docs/log/`는 상태와 실행 결과를 기록할 수 있다. `docs/roadmap/`은 의존 순서를
기록하지만 구현 성공을 주장하지 않는다.

## 실패 조건

다음 조건은 gate 실패다.

- 필수 front matter 누락
- 정본 문서의 hard-forbidden 상태성 문장
- hard-cut 대상 OS identifier가 active path 또는 file content에 존재
- public header Doxygen 누락 후보가 기준선을 초과
- source function Doxygen 누락 후보가 기준선을 초과
- Sphinx warning
- Doxygen XML 생성 실패

Doxygen 기준선은 기존 부채 상한이다. Cleanup 변경은 누락 수와 기준선을 함께 낮춘다.
기준선 증가는 문서 품질 회귀다.

## Hard-forbidden 표현

- 작업 라운드 또는 다음 작업을 보고하는 표현
- `TODO`, `FIXME`, `workaround`, `for now`
- 특정 commit이나 branch가 설계를 소유한다고 표현하는 문장
- QEMU, package, fixture를 hardware 지원으로 승격하는 문장

상태성 검토 단어는 report할 수 있으나 architecture state와 lifetime 문맥인지 사람이
확인한다.

## 빌드

`make docs`는 lint, Doxygen, Sphinx를 순서대로 실행한다. Sphinx는 `-W --keep-going`으로
warning을 오류로 취급한다. Output은 `build/docs/`에만 생성한다.

`make legacy-hard-cut`은 active source, 문서, build graph의 path와 content를 검사하되
`.git`, `build`, `__pycache__`만 제외한다. 대상 identifier를 검사기 source에 평문으로
넣지 않아 self-match 예외를 만들지 않는다.
