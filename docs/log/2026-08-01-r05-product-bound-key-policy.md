---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-08-01
code_paths:
  - include/Ribon/security/key_policy.h
  - src/security/key_policy.c
  - tools/generate_plugin_registry.py
  - products/validation/ribos-qemu/product.c
  - tools/lint/key_policy_graph_lint.py
tests:
  - make check-security-key-policy
  - make check-security-key-policy-sanitizer
  - make check-security-key-policy-graphs
  - make check-ribos-r18
hardware:
  - not-run
supersedes:
  - none
---

# R05 product-bound key policy 구현 기록

## 구현

R05는 product manifest에서 bounded immutable trust store를 생성하고 native authorizer가 이를
독립 검증하는 key-policy 계층을 추가했다. Store는 최대 32 record, record당 최대 네 domain과
root 아래 최대 두 delegation edge를 허용한다. Rotation overlap, active/retiring/revoked lifecycle,
issuer revocation closure와 mode/usage/product/domain/sequence authority를 wall clock과 heap 없이
검사한다.

Composer는 duplicate key ID/public-key identity, unknown issuer, cycle, depth overflow와 issuer보다
넓은 child authority를 거부한다. Generated table과 runtime은 같은 pointer-free canonical
serialization digest를 독립 계산한다. R18 product의 hardcoded public-key selection은 generated
store와 `ribon_key_policy_verify()`로 hard cut됐다. Ribos 쪽에는 public key나 store pointer가 아닌
key/store identity receipt만 전달된다.

## 검증 결과

- Host unit: 4-record, two-edge delegation, rotation 전/중/후, retiring/revoked key와 revoked issuer
  descendant rejection
- Negative manifest corpus: duplicate ID/key, cycle, unknown issuer, depth overflow, authority expansion,
  normal-role leakage와 provider/store mismatch 거부
- Cryptographic ordering: key-policy 실패에서 provider callback 미호출, 성공 경로는 RFC 8032
  Ed25519 signature 검증
- Sanitizer: AddressSanitizer와 UndefinedBehaviorSanitizer
- Product closure: AMD64, AArch64, RISC-V64 generated store, runtime validator와 normal-only authority
  일치; mutable store API와 Ribos raw-key authority 부재
- R18: 세 guest가 동일 generated key policy, signed artifact와 semantic receipt를 실행

## 증거 경계

이 기록은 host unit/sanitizer, target compile/object graph와 QEMU diagnostic evidence를 구분한다.
Private-key custody, trust-store OTA transaction, protected rollback journal, secure element/RPMB와 physical
hardware를 실행하거나 증명하지 않았다. Public RFC fixture key와 production-class verifier 선택은
production key management claim이 아니다.
