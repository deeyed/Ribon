---
doc_type: canonical
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
  - flat Ribon documentation conventions
---

# Ribon 문서 정책

Ribon에서 문서는 부트 및 복구 구조의 일부다. 정본 설계, ABI 계약, 결정 기록,
개발 순서, 실행 증거는 같은 권위를 갖지 않는다.

## 권위 순서

문서와 구현이 충돌할 때 다음 순서를 사용한다.

1. `docs/contracts/`와 `docs/canonical/`의 상태 없는 규칙
2. `docs/policy/`의 문서 및 품질 규칙
3. supersede되지 않은 accepted ADR
4. 소스, build graph, active test가 제공하는 구현 증거
5. `docs/platforms/`의 플랫폼별 적용 규칙
6. `docs/log/`의 checkout 및 실행 기록
7. `docs/roadmap/`, `docs/ideas/`, `docs/references/`의 비정본 자료

Ribon의 정본 문서가 Parus kernel ABI를 정의할 때에는 Parus의 대응
`docs/contracts/`와 같은 변경 단위로 동기화한다.

## 문서 타입

- `canonical`: Ribon이 따르는 상태 없는 구조와 책임 모델
- `contract`: ABI, 수명, 순서, 실패, 검증 의무를 고정하는 코드 계약
- `adr`: 채택한 결정과 기각한 대안을 보존하는 기록
- `roadmap`: 목표와 의존 순서를 설명하는 비정본 문서
- `idea`: 채택 전 설계 후보
- `devlog`: checkout, commit, 실행, 패키지, 실기기 증거 기록
- `reference`: 외부 표준과 비교 자료

Accepted ADR의 결론은 본문을 고쳐 뒤집지 않는다. 다른 결정을 채택하려면 새 ADR이
기존 ADR을 명시적으로 supersede한다.

## Front matter

모든 Markdown 문서는 다음 필드를 가진다.

```yaml
doc_type: canonical | contract | adr | roadmap | idea | devlog | reference
status: draft | accepted | experimental | superseded | obsolete
authority: normative | informative | historical
last_verified: 2026-07-26
code_paths:
  - src/core/
tests:
  - ribon-docs
hardware:
  - none
supersedes:
  - none
```

`authority: normative`는 구현 의무를 나타낸다. `informative`는 이해와 순서를 돕고,
`historical`은 특정 시점의 사실을 보존한다.

## 상태 없는 정본

`canonical`, `contract`, `policy` 문서는 checkout이나 작업 진행 상태를 보고하지 않는다.
다음 내용은 `log` 또는 `roadmap`에 둔다.

- 작업 라운드, 세션, 브랜치, commit에 묶인 문장
- 구현 완료율과 다음 작업 보고
- 특정 QEMU, 패키지, UART 실행 결과
- 단기 우회와 임시 freeze
- 구현되지 않은 기능을 성공처럼 읽히게 하는 문장

`현재 exception level`, `temporary handoff`, `deferred mapping install`처럼 architecture
state, 수명 class, non-claim을 가리키는 표현은 계약에 사용할 수 있다.

## 언어와 제목

한국어를 정본 언어로 사용한다. ABI 이름, register, marker, 표준, 고유명사는 영어를
사용할 수 있다. 제목은 한국어 명사구를 우선한다.

## Sphinx, Breathe, Doxygen

일반 문서는 MyST Markdown으로 작성한다. Doxygen은 공개 C API에서 XML을 생성하고
Sphinx는 Breathe로 XML을 포함한다. 문서 빌드는 Ribon 실행 산출물의 기본 빌드와
분리한다.

`make docs`는 다음 순서를 지킨다.

1. 문서 품질 lint
2. Doxygen XML 생성
3. Sphinx HTML warnings-as-errors 빌드

생성물은 `build/docs/` 밖에 쓰지 않는다.

## Doxygen 주석

공개 header의 함수, 타입, enum, macro는 한국어 Doxygen 주석을 가진다. 기본 형식은
`/** ... */`이다.

```c
/**
 * @brief 검증된 payload에서 OS별 handoff를 생성한다.
 *
 * @param plan Core가 동결한 부트 계획.
 * @param buffer 호출자가 소유하는 고정 용량 출력 버퍼.
 * @return 계약을 만족하면 `RIBON_STATUS_OK`를 반환한다.
 */
int ribon_profile_build_handoff(
    const struct RibonBootPlan *plan,
    struct RibonBuffer *buffer);
```

모든 C 함수 정의에도 가까운 위치에 한국어 Doxygen 주석을 둔다. 주석은 동작 요약보다
다음 계약을 우선한다.

- 호출 가능한 boot 또는 recovery phase
- allocation 가능 여부와 고정 용량
- interrupt enable 상태
- 입력과 출력의 ownership 및 lifetime
- MMIO, page table, cache, watchdog register 의미
- 실패 시 state transition과 fail-closed 결과

함수 내부의 한 줄 구현 메모는 `//`를 사용할 수 있다. 두 줄 이상의 상태 전이,
메모리 수명, lock 순서, register 의미는 Doxygen block으로 작성한다.

## 증거 분리

문서와 보고는 다음 증거 class를 구분한다.

- `not-run`
- `compile-only`
- `unit`
- `qemu-smoke`
- `qemu-runtime`
- `package` 또는 `prehardware`
- `fixture-replay`
- `hardware`

QEMU 성공은 BIOS 실기기, RPi5, RISC-V 보드 성공을 대신하지 않는다. 패키지 생성은
부팅 성공을 뜻하지 않는다.
