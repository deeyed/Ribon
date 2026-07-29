---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-30
code_paths:
  - docs/canonical/language/
  - docs/contracts/language/
  - language/
  - tools/ribosc/
tests:
  - ribos-grammar-generation
  - ribos-parser-conformance
  - ribos-negative-syntax
  - ribon-docs
hardware:
  - none
supersedes:
  - generic Lua-like policy language candidate
  - Python subset policy language candidate
  - WebAssembly-first policy source language candidate
---

# ADR: Ribos bounded policy language를 채택한다

## 맥락

Ribon은 boot source 선택, board adaptation, 검증된 image 조합, update, recovery와
handoff 생성을 정적인 C plugin만으로 표현할 수 있다. 그러나 제품별 정책과 같은
SoC의 board variant를 모두 C 조건문으로 표현하면 다음 문제가 생긴다.

- policy 수정이 Ribon Core와 driver 재빌드로 이어진다.
- mechanism을 구현하는 C 코드와 제품별 선택 순서가 결합된다.
- update와 recovery 정책의 권한 및 실행 상한을 별도로 검증하기 어렵다.
- firmware application과 firmware personality가 같은 policy 의미를 재사용하기 어렵다.
- 대화형 진단 명령과 자동 policy가 서로 다른 command subsystem을 만들기 쉽다.

범용 Lua, Teal, Python과 WebAssembly는 각각 유용하지만 Ribon의 source language로
그대로 채택하기에는 불필요한 의미가 크다.

- Lua는 동적 table, closure, garbage collection, metatable과 동적 오류 모델을 가진다.
- Teal은 Lua 생태계의 정적 검사 계층이며 bounded no-heap runtime이나 Ribon helper
  capability를 정의하지 않는다.
- Python은 exception, 동적 object model, comprehension, iterator protocol과 광범위한
  표준 의미를 가진다.
- WebAssembly는 검증된 실행 format으로 활용할 수 있지만 board와 boot policy를
  사람이 작성하는 source language의 도메인 타입과 helper 의미를 제공하지 않는다.

Ribon에는 범용 언어 호환성보다 작은 문법, 정적 타입, bounded collection, 명시적
효과와 검증된 handle이 중요하다.

## 결정

Ribon 전용 source language의 이름을 **Ribos**로 정한다.

`Ribos`는 생물학의 ribose에서 의도적으로 가져온 고유명사다. Ribose가 RNA 구조의
backbone을 이루듯 Ribos program은 Ribon의 mechanism을 조합하는 작고 제한된 policy
backbone을 표현한다. 이름은 생물학 용어를 그대로 표기한 것이 아니라 Ribon 생태계에
맞춘 독립 언어 이름이다.

공식 표기 규칙은 다음과 같다.

- 언어 이름은 `Ribos`다.
- 한국어 표기는 `리보스`다.
- `RibOS`, `RIBOS`, `Ribose`를 공식 언어 이름으로 사용하지 않는다.
- source file extension은 `.ribos`다.
- compiler command와 component prefix는 `ribosc`와 `ribos_`를 사용한다.
- Ribon은 runtime/library 제품 이름이고 Ribos는 그 위에서 검증되는 source language다.

Ribos는 Python, Rust, Lua 또는 Mojo의 subset이나 호환 구현을 주장하지 않는 독립
언어다. 다음 surface 조합을 채택한다.

1. `def`, `return`, `and`, `or`, `not`, `in`, named argument와 collection literal은
   Python과 유사한 읽기 경험을 제공한다.
2. 모든 block은 `{ ... }`로 구분하고 indentation에는 문법 의미를 주지 않는다.
3. binding은 `let`으로 선언하며 변경이 필요하면 `let mut`를 명시한다.
4. 조건식을 값으로 사용할 때에는
   `if condition { value } else { value }` 한 형태만 허용한다.
5. `struct`는 고정 layout의 value type이고 `enum`은 폐쇄된 tagged type이다.
6. `Array[T, N]`, `List[T, N]`, `FrozenMap[K, V, N]`,
   `Dict[K, V, N]`은 type에 capacity를 포함한다.
7. 모든 container는 동종 element 또는 동종 key/value를 가진다.
8. `Option[T]`, `Result[T, E]`, 제한된 `match`와 postfix `?`로 복구 가능한 실패를
   표현한다.
9. exception, heap, recursion, indirect call, closure, inheritance, reflection,
   dynamic import와 unbounded loop는 언어에 존재하지 않는다.
10. policy attribute는 `@policy(...)`처럼 compiler가 인식하는 폐쇄된 declaration
    metadata다. Python식 runtime decorator는 제공하지 않는다.
11. helper는 raw MMIO, raw flash address와 arbitrary jump가 아닌 semantic typed
    operation만 제공한다.
12. 검증 결과는 `bool`이 아니라 `VerifiedImage` 같은 typed handle로 승격한다.

Python식 conditional expression인 `a if condition else b`는 채택하지 않는다.
Rust식 expression-if와 두 형태를 동시에 제공하면 formatter, multiline rule과 학습
표면이 중복되기 때문이다.

일반 heterogeneous dict와 `Any` type은 채택하지 않는다. Handoff와 boot metadata는
typed key 또는 schema-generated record로 작성한다.

## Parser와 실행 경계

Ribos grammar의 정본 형식은 Pegen grammar다. Pegen은 host-side parser generator로
사용한다. CPython AST, Python object model, CPython tokenizer와 CPython parser
runtime은 Ribos의 언어 의미나 Ribon runtime ABI가 아니다.

Production boot product는 `.ribos` source를 필수 입력으로 해석하지 않는다. Host
compiler가 source를 typed IR과 signed policy artifact로 변환하며 Ribon product는
artifact의 구조, signature, verifier certificate와 bytecode를 검증한다.

Source parser를 포함하는 developer tool이나 interactive shell은 별도 product
selection이다. Firmware 안에 parser가 포함될 때에는 fixed token storage, fixed AST
arena, fixed memo budget과 deterministic capacity failure를 사용한다.

## 결과

- 언어 문법은 Python 호환성 부채 없이 도메인 요구에 맞게 발전할 수 있다.
- `let`과 `let mut`가 data-flow와 mutation 검증을 단순화한다.
- bounded collection과 bounded loop가 실행량 상한 계산의 입력이 된다.
- typed helper와 typestate handle이 capability 및 trust 불변식을 source type에
  반영한다.
- 같은 language model을 board policy, boot policy, update policy와 제한된 shell에
  사용할 수 있다.
- Host compiler와 firmware VM을 분리하여 source parser가 production boot TCB에
  필수로 들어가지 않는다.

이 ADR은 policy bytecode encoding, signature container, VM opcode와 verifier
algorithm을 정의하지 않는다. 해당 경계는 독립 contract와 ADR로 동결한다.

## 기각한 대안

### 범용 Lua fork

Lua parser를 재사용할 수 있지만 dynamic table, closure, metatable, GC와 오류 모델을
제거한 뒤 정적 타입과 bounded container를 추가하면 원본 runtime의 이점이 작아진다.
Ribos 문법과 verifier 의미를 독립적으로 정의하는 방식을 선택한다.

### Teal fork

Teal의 type annotation 경험은 참고할 수 있지만 Lua source와 Lua runtime 호환을
보존하는 목표가 Ribon의 no-heap, typed helper와 bounded execution을 대신하지 않는다.

### Python subset

Python syntax 일부는 친숙하지만 Python compatibility를 약속하면 exception, object
model, iterator, truthiness와 library 기대가 언어 경계로 들어온다. Ribos는 유사한
surface만 선택하고 독립 의미를 정의한다.

### WebAssembly source model

WebAssembly는 별도의 portable policy artifact 후보가 될 수 있다. 그러나 사람이
작성하는 source와 semantic helper type을 WebAssembly 자체로 표현하면 board 및 boot
domain의 정적 오류가 저수준 import signature와 linear memory 문제로 이동한다.

### JSON 또는 YAML policy

두 format은 설정 자료에는 적합하지만 control flow, typed result, bounded iteration과
helper composition을 표현하는 실제 언어 요구를 충족하지 않는다.
