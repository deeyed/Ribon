---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/vm/include/ribos/vm/prepared.h
  - language/ribos/vm/src/prepared.c
  - docs/contracts/language/ribos-prepared-program-v1.md
tests:
  - make check-ribos-prepared-program
  - make check-ribos-verifier
  - make check-ribos-host-boundary
  - make check
  - make docs
hardware:
  - none
supersedes:
  - implicit verifier-report execution authorization
---

# ADR 0026: AuthorizedArtifact와 PreparedProgram으로 실행 자격 봉인

## Context

Structural reader, independent verifier와 runtime ABI가 존재해도 caller가 같은 mutable
artifact byte를 verification 뒤 dispatch에 다시 제공하면 TOCTOU가 남는다. Verifier
report만 cache하면 report가 어느 exact artifact, schema, helper implementation과
effective limit에 속하는지 실행 시점에 보장할 수 없다.

Signature와 rollback authority를 generic VM에 넣으면 product trust root, lifecycle과
secure storage가 architecture-neutral interpreter에 결합된다. 반대로 callback의
boolean 승인만 보존하면 helper table 또는 schema identity가 authorization 뒤 바뀌어도
실행 자격이 유지될 수 있다.

## Decision

Authorization과 execution preparation을 두 opaque caller-owned lifetime으로 나눈다.

- `RibosAuthorizedArtifact`는 structural-open된 exact artifact copy와 product authority
  receipt를 봉인한다.
- Product callback이 signature, key selection, manifest sequence와 rollback floor를
  판단한다.
- `RibosPreparedProgram`은 artifact를 다시 복사하고 Stage-1과 Stage-2를 copied range에
  실행한다.
- Selected schema는 canonical identity만 보존하고 pointer를 retain하지 않는다.
- Helper execution descriptor와 callback table은 PreparedProgram workspace에 복사한다.
- Receipt는 artifact, schema와 helper execution digest를 함께 승인한다.
- Prepared binding은 artifact hash, schema digest, helper digest, runtime ABI와
  effective limits를 canonical SHA-256으로 묶는다.
- Production execute API는 raw byte나 verifier report가 아닌
  `RibosPreparedProgram`만 받는다.
- Workspace size와 alignment query는 state를 만들지 않으며 target implementation은
  heap allocation을 하지 않는다.
- Exact whole-artifact digest는 signature/key-ID byte mutation도 검출한다. Runtime
  canonical binding은 기존 계약대로 executable payload hash를 사용한다.

## Consequences

- Authorization, verification과 runtime execution이 별도 claim으로 유지된다.
- Original artifact, schema와 helper table의 prepare 이후 mutation이 execution state에
  전파되지 않는다.
- Product trust implementation 없이도 generic VM의 authority injection boundary를
  host fixture로 검증할 수 있다.
- Prepared workspace는 artifact와 callback table copy 때문에 더 크지만 ownership과
  lifetime이 명확하고 동적 allocation이 필요 없다.
- Schema descriptor 전체를 runtime에 복사하지 않아 target memory를 줄이며 verifier
  이후 source-level schema mutation surface를 제거한다.
- R09 interpreter는 PreparedProgram의 immutable view만 소비해야 하며 raw loader를
  추가할 수 없다.
- 이 결정만으로 callback 구현의 안전, platform memory corruption, signature provider
  또는 runtime counter 집행이 증명되지는 않는다.

## 기각한 대안

### Verifier report를 execution certificate로 사용

Report가 artifact byte, product authorization, helper callback과 limit에 결박되지
않으므로 기각한다.

### Execute 시 raw artifact를 다시 verify

매 execute마다 비용이 반복되고 authorization과 dispatch 사이 alias가 남으며 public
API가 raw dispatch를 허용하므로 기각한다.

### Generic VM에 Ed25519 key store와 rollback counter 구현

Product trust root와 persistent-state policy를 architecture-neutral runtime에 결합하므로
기각한다.

### Artifact pointer를 read-only라고 선언하고 borrow

C pointer qualifier는 backing storage의 실제 immutability나 다른 alias를 보장하지
않으므로 default production path로는 기각한다.

### Callback address를 canonical helper digest에 포함

ASLR, relocation, architecture와 process lifetime에 따라 identity가 바뀌고 artifact
portability를 깨뜨리므로 기각한다. Callback pointer는 copied process-local binding으로
보호한다.

### Product schema descriptor 전체를 PreparedProgram에 deep copy

Stage-2 이후 runtime이 source spelling과 pointer-rich descriptor를 필요로 하지 않는다.
Canonical digest만 보존하는 편이 더 작고 mutation surface가 적으므로 기각한다.
