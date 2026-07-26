---
doc_type: devlog
status: accepted
authority: evidence
last_verified: 2026-07-26
code_paths:
  - include/Ribon/
  - src/core/
  - src/common/
  - src/arch/
  - src/environments/host/
  - src/protocols/
  - qstar/
  - Makefile
tests:
  - make check
  - make docs
  - qstar --file qstar.lua check
  - git diff --check
hardware:
  - none
supersedes:
  - none
---

# R3 Library와 Plugin Protocol hard cut 구현 기록

## 범위

R3는 profile과 firmware adapter를 중심으로 구성되던 내부 ABI를 제거하고, generic
library와 typed plugin protocol을 기준으로 Ribon의 활성 소스 트리를 다시 구성했다.
Parus kernel consumer, UEFI·BIOS firmware product, Raspberry Pi boot product는 이
라운드의 변경 또는 실행 검증 범위가 아니다.

## 구현

- `libribon-core`는 caller-owned memory, capability, context, plugin descriptor,
  manifest, immutable registry 검증만 소유한다.
- `libribon-boot`는 service table 정규화, environment binding, boot protocol 선택,
  boot plan 상태 전이만 소유한다.
- Architecture, Environment, Image Format, Boot Protocol은 동일한 descriptor
  envelope를 사용한다. descriptor에는 구조체 크기, Core ABI 범위, stable plugin ID,
  kind, phase, capability, dependency, resource budget, deadline이 포함된다.
- Registry 검증은 중복 ID, 필수 provider 부재, capability 부재, dependency cycle,
  Core ABI 불일치, product budget 초과를 거부한다.
- QStar는 `qstar/manifests/host-reference.json`을 입력으로 build-selected immutable
  registry C source를 생성한다. weak symbol이나 constructor discovery는 사용하지
  않는다.
- Host reference product는 Core와 Boot archive를 실제로 링크하고, host
  environment, Architecture, ELF64 image format, synthetic boot protocol을 생성된
  registry로 결합한다.
- Parus RPH1은 Core의 특수 분기가 아니라 Boot Protocol plugin의 private wire
  contract로 이동했다.
- 기존 flat public header, `RibonProfile`, `RibonFirmwareAdapter`, profile path와
  관련 QStar graph를 활성 트리에서 제거했다.

## 검증 해석

`make check`는 x86_64, AArch64, RISC-V 64 host reference product와 Core/Boot archive
object graph, plugin descriptor 음성 사례, protocol contract, protocol-free embed,
ELF64와 RPH1 unit test를 실행했다. 세 host product는 synthetic protocol로 plan
prepare, commit, quiesce 상태까지 진행했으며 실제 privileged transfer는 하지
않았다.

`qstar --file qstar.lua check`는 세 architecture별 생성 registry action과 20개
target closure를 검사했다. hard-cut lint는 활성 public header와 source/build graph에
과거 profile·adapter ABI가 남지 않았음을 검사한다.

이 결과는 host unit/compile/link 증거다. UEFI·BIOS firmware 실행, QEMU kernel boot,
Raspberry Pi package/replay, live hardware UART 성공을 주장하지 않는다.
