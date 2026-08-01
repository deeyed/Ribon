---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-08-01
code_paths:
  - include/Ribon/update/manifest.h
  - src/update/manifest.c
  - tools/update_manifest.py
tests:
  - make check-update-manifest
  - make check-update-manifest-sanitizer
  - make check-update-manifest-cross-compile
hardware:
  - none
supersedes:
  - implicit JSON update metadata
---

# ADR 0046: Update authority는 별도 canonical manifest와 signed-message domain을 사용한다

## Context

Ribos policy trust는 product, schema, mode, key usage와 rollback domain을 canonical message에
결속한다. Update bundle도 같은 security provider와 key-policy registry를 사용할 수 있지만
policy artifact와 component bundle은 version, target binding, payload table과 실행 시점이 다르다.
Ribos artifact message를 update에 재사용하면 object class와 key usage 경계가 흐려진다.

JSON source metadata는 host assembly에 편리하지만 field order, whitespace, number encoding과 path가
target wire identity로 적합하지 않다. Native C struct dump도 endian, padding, pointer와 ABI width에
의존한다. Download나 storage 구현 전에 independent reader가 재유도할 bounded byte contract가
필요하다.

## Decision

- Update manifest v1은 256-byte header와 canonical two-entry section directory를 사용한다.
- Product/architecture/platform/environment/protocol/rollback binding은 exact 256-byte section이다.
- Component는 최대 16개의 exact 192-byte row이며 semantic role과 destination만 노출한다.
- 모든 wire integer는 explicit little-endian codec로 읽고 쓴다.
- Update signature는 `RIBON-UPDATE-MESSAGE-V1` 256-byte domain과
  `UPDATE_MANIFEST` single key usage를 사용한다.
- Detached envelope는 key ID와 Ed25519 signature만 운반하며 private key와 signer는 host에 남긴다.
- Target authorizer는 compiler output을 신뢰하지 않고 manifest와 envelope의 모든 range, reserved,
  digest와 identity를 다시 유도한 뒤 기존 key-policy와 selected Ed25519 provider를 호출한다.
- Source JSON은 exact component read를 위한 host input일 뿐 target wire나 runtime parser가 아니다.
- Compatibility decoder, policy/update dual usage와 unsigned production fallback은 두지 않는다.

## Consequences

Update, Ribos policy와 boot image는 같은 bounded key-policy 및 signature mechanism을 공유하면서도
canonical message domain과 key usage로 object authority를 분리한다. Component format과 target tuple을
storage/network 구현보다 먼저 freeze하므로 이후 installer는 verified manifest view만 소비할 수 있다.

Manifest v1은 component payload를 저장하지 않으며 rollback journal을 읽지 않는다. A/B layout,
transactional writer, recovery network와 OS confirmation은 각각 독립 계약과 실행 evidence를 요구한다.

## 기각한 대안

### Ribos trust message의 artifact field에 manifest digest 대입

VM ABI와 ISA field가 update object에 의미가 없고 policy/update usage confusion을 만들므로 기각한다.

### JSON을 서명하고 target에서 JSON parse

Canonicalization과 parser surface가 커지고 source path와 host-only metadata가 wire authority에 들어가므로
기각한다.

### Packed C struct를 저장

Architecture ABI, endian과 padding에 따라 identity가 달라지고 independent reader가 range를 재유도하기
어려우므로 기각한다.

### Manifest와 signature를 하나의 mutable blob으로 저장

Offline signer input과 transport/storage envelope의 lifetime을 분리하기 어렵고 재서명 없이 component
manifest를 비교하기 어려우므로 detached envelope를 사용한다.
