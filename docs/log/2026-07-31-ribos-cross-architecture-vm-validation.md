---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-07-31
code_paths:
  - Makefile
  - products/validation/ribos-qemu/
  - targets/ribos-validation/
  - tools/make_ribos_signed_fixture.py
  - tools/make_ribos_qemu_manifest.py
  - tools/ribos_cross_arch_qemu.py
  - tools/lint/ribos_cross_arch_object_lint.py
tests:
  - make check-ribos-golden-artifact
  - make check-ribos-cross-arch-objects
  - make check-ribos-cross-arch-qemu
  - make check-ribos-r18
  - make check
  - make docs
hardware:
  - not-run
supersedes:
  - none
---

# Ribos 교차 architecture VM 검증 구현 기록

## 구현

Host compiler가 `aggregate_ownership.rbs`에서 만든 동일 `.rba`를 AMD64 UEFI,
AArch64 raw-FDT와 RISC-V OpenSBI target image에 embed했다. 세 image는 target별로
cross-compiled한 `libribos-target-core.a`, generic Ribon adapter와 generated
`ribos-qemu-validation` product binding을 링크한다.

Artifact는 deterministic signed-envelope fixture와 golden SHA-256을 사용한다. Signature
field는 Ed25519 wire shape지만 cryptographic signature가 아니며 validation authorizer는
exact test key와 byte를 비교한다.

Guest fixture는 semantic helper 네 번, opaque handle, BootAction single consume,
metadata commit, flush, quiesce와 watchdog을 실행한다. Signature, payload, schema,
instruction budget와 deadline negative case는 compiled factory fallback으로 닫는다.
Product graph과 QEMU command 모두 network를 포함하지 않는다.

## AArch64에서 발견한 build 결함

첫 실행에서 AMD64와 RISC-V는 완료했지만 AArch64는 synthetic ELF를 byte-wise로 만드는
중 정렬 예외로 정지했다. C source는 `uint8_t` loop였으나 optimizer가 비정렬 wide
store로 결합했다.

AArch64 freestanding flag에 `-mstrict-align`을 추가하고 R18 target object rule이
Makefile 변경을 prerequisite로 소비하도록 수정했다. 모든 AArch64 Core, adapter와 VM
object를 새 flag로 다시 compile한 뒤 같은 guest sequence가 완료되었다.

## 검증 결과

- golden signed artifact SHA-256:
  `f94f79585c38c574372517dc2f10e5919c6e72e0f7b84990ccaf60337db54b3e`
- artifact byte 크기: 10,293
- cross-target object graph: host/frontend/network import 0
- AMD64, AArch64, RISC-V marker sequence: 동일
- semantic receipt:
  `v1-stage8-action21-helpers4-fallback0`
- signature, corrupt payload, schema, budget와 deadline fallback: 각 target에서 통과
- physical hardware: 실행하지 않음

Machine-readable result와 raw serial log는 build output의
`build/results/ribos-r18/`에 생성된다. Build output은 source 정본이 아니며 매 gate에서
다시 생성한다.

## 증거 한계

이 기록은 QEMU guest-executed policy와 cross-compiled object evidence다. OS payload
transfer, production Ed25519, secure rollback, recovery networking, OTA flash, UEFI
firmware provider와 physical hardware를 실행하지 않았다.
