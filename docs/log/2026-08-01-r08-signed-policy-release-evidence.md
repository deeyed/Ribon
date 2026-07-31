---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-08-01
code_paths:
  - Makefile
  - products/validation/ribos-qemu/
  - tools/ribos_cross_arch_qemu.py
  - tools/check_ribos_release_reproducibility.py
tests:
  - make check-ribos-cross-arch-qemu
  - make check-ribos-release-reproducibility
  - make check-ribos-production-policy
hardware:
  - not-run
supersedes:
  - none
---

# R08 signed policy release evidence 실행 기록

## 구현 결과

- R18 validation product가 sequence 18 confirmed와 sequence 19 trial artifact를 같은 source, schema,
  key policy와 rollback domain으로 embed한다.
- AMD64 UEFI, AArch64 raw와 RISC-V OpenSBI guest가 generic signed-policy adapter, production-class
  Ed25519 verifier와 reference protected-state journal을 실행한다.
- Target negative corpus는 signature, payload, truncation, product, schema, key, sequence, state, budget와
  deadline을 fail closed한다.
- Target object gate는 signer/private fixture authority, host frontend/IR, network와 hosted import를
  거부한다.
- Aggregate gate `check-ribos-production-policy`와 clean-root release reproducibility gate를 추가했다.

## QEMU runtime evidence

세 target은 exact receipt
`receipt=v1-stage8-action21-helpers4-fallback0`와 같은 20-marker sequence를 각각 한 번 기록했다.
Confirmed commit, ten negative fallback, trial confirmation, failed-trial 뒤 confirmed fallback과 normal-mode
network 부재가 실행되었다. Timeout, forbidden marker와 forced kill은 없었고 세 process group cleanup이
완료되었다.

Confirmed artifact SHA-256은
`2cdf299aaf59fe85df1f3335c14ab94db3a35bd16b7688b2bf8433841e7972ce`, trial artifact SHA-256은
`cf7249368a17602f6731bbe30796146310fb255acfe351429fa6028e3649ed74`였다.

결과는 `build/results/ribos-r18/ribos-r18-cross-architecture.json`과 같은 디렉터리의 target별 raw
serial log에 기록되었다.

## Reproducibility evidence

두 개의 clean independent build root에서 product manifest, signed artifact 두 개, embedded source 두 개,
target registry 세 개와 AMD64/AArch64/RISC-V image를 포함한 13개 canonical output의 hash가 같았다.
Release-set SHA-256은
`4a6c7989ca0ed6945c9ceb7aace4d25caf04d8d9d7d26b92806846628068ca6f`였다. 결과와 command log는
`build/tests/ribos-release-reproducibility/`에 기록되었다.

## 증명하지 않는 것

Physical hardware는 실행하지 않았다. Public RFC test vector와 reference memory journal은 production
key custody, TPM/RPMB anti-replay 또는 hostile-media durability가 아니다. 실제 OTA transport/flash,
OS payload transfer와 external health confirmation도 이 실행 증거에 포함되지 않는다.
