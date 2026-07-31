---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-08-01
code_paths:
  - include/Ribon/policy/ribos.h
  - src/plugins/policy/ribos/adapter.c
  - tools/generate_plugin_registry.py
  - tests/policy/ribos_integration_tests.c
tests:
  - make check-ribos-ribon-integration
  - make check-ribos-r18
hardware:
  - not-run
supersedes:
  - none
---

# R07 signed Ribos authorization 실행 기록

## 구현 결과

- Product graph의 임의 `authorize_symbol`을 제거하고 authorization class와 signed binding을 생성했다.
- Generic adapter가 product identity, key policy, production Ed25519, protected rollback journal과
  independent verifier를 직접 결합한다.
- Host reference product는 sequence 1/2 signed artifact와 reference journal로 A/B trial, confirm과
  failed-trial fallback을 실행한다.
- R18 product의 중복 signature authorizer를 제거하고 같은 adapter 경로로 전환했다.

## 실행 증거

`make check-ribos-ribon-integration`은 signed A의 정상 commit, signed B trial의 attempt 선차감과
confirmation, 이전 sequence 거부, failed trial 뒤 A 복귀를 실행했다. Unsigned, corrupt signature,
unknown key, wrong product/schema/mode/sequence, corrupt journal과 correctly signed verifier-invalid
candidate는 helper 또는 BootAction 실행 전에 fail closed했다.

`make check-ribos-r18`은 같은 signed artifact와 generic adapter를 AMD64 UEFI, AArch64 raw-FDT와
RISC-V OpenSBI QEMU guest에서 실행했다. 세 guest의 semantic receipt는 equivalent였다.

## 증명하지 않는 것

Reference in-memory journal은 hardware anti-replay 또는 physical power-loss durability가 아니다.
시험용 RFC 8032 seed는 production key custody가 아니다. Recovery network/flash, 실제 OTA media,
OS health confirmation, physical board 실행은 이 라운드의 claim이 아니다.
