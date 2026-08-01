---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-08-01
code_paths:
  - include/Ribon/update/manifest.h
  - src/update/manifest.c
  - tools/update_manifest.py
  - tests/update/
tests:
  - make check-update-manifest
  - make check-update-manifest-sanitizer
  - make check-update-manifest-cross-compile
hardware:
  - not-run
supersedes:
  - none
---

# D01 signed update manifest v1 실행 기록

## 구현 결과

- 256-byte header와 canonical section directory, 256-byte product binding 및 최대 16개의
  192-byte component entry를 explicit little-endian codec로 구현했다.
- Update 전용 256-byte signed message와 detached Ed25519 envelope를 기존 immutable key policy 및
  production-class provider에 연결했다.
- Host assembler는 component를 exact read하고 expected SHA-256, maximum size, singleton과 range를
  검사한다. Inspector와 offline-signer message surface는 같은 wire를 독립적으로 decode한다.
- Canonical vector는 kernel과 Ribos policy fixture 두 개를 사용한다. Manifest SHA-256은
  `83a9f2ac4d9192575398974b194cbf2fb084cfc42b4dcf81302a2d124731b5e9`, signed-message
  SHA-256은 `998c4206dfce229494de35bd06f5d2479ccc72fca5e0d3d7574a558c4a3e9eb2`다.
- OpenSSL offline signer와 target production Ed25519 provider가 같은 message와 signature를
  교차 검증한다.

## Host evidence

Focused gate는 deterministic C/Python codec 두 개, 18개 structural wire hostile case, 5개 source
manifest hostile case와 signature/product/domain/sequence 4개 authorization mutation을 검사한다.
Sanitizer binary는 같은 native codec와 envelope mutation corpus를 실행한다. Manifest와 SHA-256
target source는 freestanding x86_64, AArch64, RISC-V 64 object로 교차 컴파일했다.

## 증명하지 않는 것

Hardware는 실행하지 않았다. Storage writer, A/B slot, network fetch, protected rollback transition,
physical power-loss durability, production key custody와 RPi5 boot는 이 evidence에 포함되지 않는다.
