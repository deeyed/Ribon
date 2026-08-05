---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-08-05
code_paths:
  - Makefile
  - make/
  - tools/make/
  - .github/workflows/ci.yml
  - qstar.lua
tests:
  - make check-build-system
  - make check
  - make check-target-builds
  - make ci-qemu
  - make docs
hardware:
  - none
supersedes:
  - monolithic Makefile with Homebrew-version paths
---

# ADR-0057: 휴대형 모듈 Make와 QStar 검증 병존

## 결정

Ribon의 공개 실행 frontend는 GNU Make로 고정하고 루트 Makefile을 기능별 module include로
분해한다. QStar graph는 product/plugin composition의 독립 검증기로 유지한다.

Compiler, linker, binary utility와 QEMU는 PATH 이름을 기본값으로 사용한다. OVMF와
OpenSBI는 선택한 QEMU install prefix와 표준 data root에서 탐지하며 caller override를
허용한다. 사용자 home, Homebrew Cellar version과 `/usr/bin/clang`은 build graph에
기록하지 않는다.

Ubuntu GitHub Actions는 Make target만 호출해 host, target, QEMU와 docs lane을
검증한다. QStar도 `make qstar-check`를 통해서만 CI에 노출한다.

## 이유

외부 사용자는 Ribon 내부 graph 도구를 알기 전에 표준 Make 진입점으로 library, SDK와
지원 product를 재현할 수 있어야 한다. 동시에 QStar가 제공하는 typed composition과
object closure 검사를 약화시키면 안 된다. 실행 frontend와 의미 검증기의 역할을
분리하면 두 도구의 중복 authority 없이 양쪽의 장점을 유지할 수 있다.

## 기각한 대안

- 단일 4천 줄 Makefile 유지: ownership과 변경 영향 범위를 설명하기 어렵다.
- QStar 제거: source-owned product closure의 독립 검증을 잃는다.
- Make와 QStar에 별도 product source list 유지: 같은 target 이름이 서로 다른 binary를
  뜻할 수 있으므로 기각한다.
- package-manager version 경로 자동 생성: 특정 host state를 source contract로 승격하므로
  기각한다.

## 결과

모든 public build와 test는 Make target으로 접근할 수 있다. QStar는 Make aggregate의
필수 graph gate다. 새 build backend를 추가할 때에도 product manifest를 소비해야 하며
별도 product 의미를 만들 수 없다.
