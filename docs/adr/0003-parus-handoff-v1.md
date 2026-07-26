---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-26
code_paths:
  - include/Ribon/profiles/parus/
  - src/profiles/parus/
  - ../../../../sys/include/parus/boot/
  - ../../../../sys/kern/boot/
tests:
  - ribon-parus-handoff-v1-unit
  - parus-ribon-handoff-v1-consumer
hardware:
  - none
supersedes:
  - legacy previous handoff wire semantics
---

# ADR: Parus Handoff v1을 새 wire ABI로 채택한다

## 맥락

이전 handoff는 producer header, consumer contract, entry flag, checksum 의미가 일치하지
않는다. Native struct layout과 미기록 checksum은 architecture와 compiler에 독립적인
검증 경계를 제공하지 못한다.

## 결정

Parus profile은 `RPH1` wire artifact를 생성한다. Byte-wise little-endian serialization,
고정 header와 section entry, CRC-32C, strict bounds, required-section 처리, borrowed-range
lifetime을 사용한다.

Ribon Core는 RPH1 field를 알지 않는다. Producer wire format은 Ribon Parus profile이
소유하고 Parus consumer는 같은 test vector와 malformed corpus를 소비한다.

## 기각한 대안

### 기존 wire struct 수정

같은 magic과 version의 의미가 변경되어 preserved artifact를 안전하게 거부할 수 없으므로
선택하지 않는다.

### Native C struct handoff

Padding, endian, compiler layout, pointer size가 wire ABI로 새어 나오므로 선택하지 않는다.

### Signature를 handoff에 다시 포함

Boot bundle manifest가 authenticity를 소유한다. In-memory handoff는 CRC와 bounds로
전송 무결성을 검증하고 signature verification 결과를 provenance로 전달하므로 중복
signature envelope를 두지 않는다.

## 결과

- Producer와 consumer는 한쪽만 구현된 상태를 acceptance로 보지 않는다.
- Unknown required section은 fail-closed다.
- Raw handoff pointer는 Parus early boot 뒤 runtime ABI가 되지 않는다.
- Wire number 변경은 새 major version을 요구한다.
