---
doc_type: adr
status: superseded
authority: historical
last_verified: 2026-07-26
code_paths:
  - include/Ribon/profile.h
  - include/Ribon/profiles/parus/
  - src/core/profile.c
  - src/profiles/parus.c
  - src/profiles/parus/
  - tools/lint/legacy_os_hard_cut.py
tests:
  - ribon-legacy-semantic-hard-cut-lint
hardware:
  - none
supersedes:
  - legacy OS profile compatibility policy
superseded_by:
  - 0009-limine-library-plugin-hard-cut
---

# ADR: legacy OS 의미론을 hard cut한다

## 맥락

Ribon Core의 첫 OS profile과 handoff artifact는 legacy OS 이름, flag, marker, fixture,
문서에 결합되어 있다. Parus profile을 alias로 추가하면 old/new ABI가 같은 Core에서
서로 다른 의미로 유지되고 producer와 consumer의 권위가 둘로 나뉜다.

Ribon은 외부 stable release 이전이며 legacy OS compatibility를 보존할 의무가 없다.
과거 구현은 Git history와 freeze tag로 조회할 수 있다.

## 결정

Active Ribon source, public header, profile registry, build graph, fixture, marker, 문서에서
legacy OS 의미론을 compatibility wrapper 없이 제거한다.

- `profiles/legacy-os`을 `profiles/parus`의 alias로 사용하지 않는다.
- 기존 handoff magic과 version을 Parus Handoff v1이 재사용하지 않는다.
- old/new 선택 option과 dual builder를 두지 않는다.
- 이전 marker를 Parus 성공 gate의 fallback으로 사용하지 않는다.
- 역사 설명이 필요한 이 ADR과 전환 devlog 외의 정본 문서에는 legacy identifier를 두지 않는다.

## 기각한 대안

### Profile alias

이름만 Parus로 보이게 하고 wire field를 유지하면 consumer가 어떤 의미론을 검증하는지
분명하지 않아 선택하지 않는다.

### Dual profile 장기 유지

두 profile이 loader와 direct-high 경로를 공유하면 회귀 gate와 failure vocabulary가
분기되어 Core 중립성을 오히려 약화하므로 선택하지 않는다.

### 기존 magic 재정의

같은 byte sequence가 서로 다른 계약을 뜻하게 되어 preserved fixture와 새 consumer가
구분할 수 없으므로 선택하지 않는다.

## 결과

- Parus profile과 handoff consumer를 같은 변경 계열에서 구현해야 한다.
- 이전 fixture와 문서는 active gate에서 제거된다.
- Wide compile break는 전환 변경 안에서 허용되지만 compatibility surface는 허용하지 않는다.
- Active tree의 legacy identifier는 build-independent lint 실패 대상이다.

## 집행

새 결정을 채택하려면 별도 ADR이 이 문서를 supersede해야 한다. 이 ADR의 본문을 고쳐
compatibility를 다시 열지 않는다.
