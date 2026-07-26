---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-07-26
code_paths:
  - stand/Ribon/docs/
  - stand/Ribon/Makefile
  - stand/Ribon/tools/lint/documentation_quality_lint.py
  - stand/Ribon/configs/rpi5/
tests:
  - ribon-documentation-quality-lint
  - ribon-doxygen-xml
  - ribon-sphinx-html-werror
  - ribon-rpi-package-layout
  - parus-documentation-quality-lint
  - parus-r0-new-doc-sphinx-warning-audit
  - git-diff-check
hardware:
  - none
supersedes:
  - none
---

# 2026-07-26 R0 문서 hard cut 기록

## Source 기준

- branch: `parus`
- source revision: `2a76d03744e092dc0bbeb9efde62f112815bd1e2`
- 시작 상태: tracked change 없음

## 문서 변경

기존 Ribon 문서는 하나의 평면 디렉터리에서 legacy OS profile, Round 진행 상태, QEMU marker,
legacy archive 판단, RPi5 prototype 설명을 같은 권위로 다뤘다.

R0은 기존 8개 Markdown 문서를 active tree에서 제거하고 `policy`, `canonical`,
`contracts`, `adr`, `platforms`, `roadmap`, `references`, `log`, `api` 위계로 교체한다.
RPi5 package 입력은 `docs/examples`에서 `configs/rpi5`로 이동한다.

## 구현 상태 분리

R0은 설계와 문서 tooling 변경이다. Source에는 다음 전환 부채가 남아 있다.

- builtin legacy OS profile과 legacy previous handoff builder
- Ribon과 Parus entry flag 불일치
- Parus previous handoff consumer stub
- direct-high-preferred profile policy
- BIOS unsupported adapter
- RISC-V entry stub
- update, trust, network, recovery service 부재

이 항목은 canonical 또는 contract의 구현 성공으로 해석하지 않는다. R1 이후 각
acceptance gate가 source와 runtime evidence를 별도로 닫아야 한다.

## 검증 기록

- `python3 tools/lint/documentation_quality_lint.py`: 통과
  - front matter 누락: 0
  - hard-forbidden 상태 문장: 0
  - legacy identifier 위반: 0
  - source function Doxygen 누락: 142, 기준선 142
  - public header Doxygen 누락: 36, 기준선 36
- `make docs SPHINX_BUILD=build/docs/venv/bin/sphinx-build`: 통과
  - Doxygen XML 생성: 통과
  - Sphinx `-W --keep-going` HTML: 통과
  - 공개 API HTML의 legacy profile symbol 검색: 0
- `make rpi-package-check`: `RIBON-RPI-PACKAGE-LAYOUT-OK`
- Parus `python3 tools/lint/documentation_quality_lint.py`: 통과
- Parus Sphinx full-tree strict audit: 기존 문서 경고 365개로 실패
  - 새 R0 ADR과 Parus Handoff v1 consumer 계약에 연결된 경고: 0
  - `docs/index.md`에는 기존의 존재하지 않는
    `contracts/device/qemu-timer-interrupt-test-lane` 참조 경고가 남아 있음
- `git diff --check`: 통과

Doxygen의 undocumented compound와 member 경고는 기존 source 주석 부채다. R0 gate는
누락 후보 수를 142/36에서 증가시키지 않으며, 이후 주석 cleanup은 실제 누락 수와
기준선을 함께 낮춰야 한다.
