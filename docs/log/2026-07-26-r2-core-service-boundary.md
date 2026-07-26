---
doc_type: devlog
status: accepted
authority: evidence
last_verified: 2026-07-26
code_paths:
  - include/Ribon/core.h
  - include/Ribon/platform.h
  - include/Ribon/arch.h
  - include/Ribon/profile.h
  - src/core/
  - src/modes/
  - src/profiles/parus.c
  - Makefile
  - src/core/core.qst
tests:
  - make check-core-service
  - make check-arch-ops
  - make check-mode-descriptors
  - make check-object-graphs
  - make qstar-check
  - make check
  - make docs
hardware:
  - none
supersedes:
  - none
---

# R2 Core service 경계 구현 기록

## 범위

R2는 Parus kernel consumer를 변경하지 않고 Ribon 내부 service 경계를 구현했다.
Core/Profile/Platform/Architecture ABI, fixed arena, mode별 resource limit, link object
graph를 수용 범위로 삼았다. Kernel handoff consumer와 Parus source tree는 이 기록의
검증 대상이 아니다.

## 구현

- `RibonArena`는 caller-owned storage만 사용하는 단방향 bump allocator다.
- `RibonCoreContext`는 mode, Platform/Architecture operation table, Profile, arena를
  operation 호출 전에 함께 검증한다.
- `RibonPlatformOps`는 모든 callback을 unsupported stub으로 초기화하고 capability와
  callback을 함께 승격하도록 만들었다.
- `RibonArchOps`는 payload validation, cache synchronization, direct-high preparation,
  entry bridge, halt를 capability로 표현한다.
- Parus profile은 manifest match, component validation, architecture별 register ABI,
  RPH1 builder, generation/nonce confirmation validation을 한 operation table에 제공한다.
- Make와 QStar는 normal, recovery, provisioning, diagnostic mode source를 선택적으로
  링크한다. Host Core archive에서는 UEFI, BIOS, Raspberry Pi adapter를 분리했다.
- Static archive를 다시 만들 때 이전 member가 남지 않도록 output archive를 먼저
  제거한다.
- 새 공개 API와 source function에 한국어 Doxygen을 추가하고 누락 허용 기준선을
  source 142개에서 132개로, public header 36개에서 27개로 낮췄다.

## 검증 해석

`core_service_boundary_tests`는 arena overflow와 alignment, unsupported Platform
operation, required/forbidden capability, Architecture payload validation, Parus
manifest/component/entry/confirmation 계약을 검사한다.

`mode_descriptor_tests`는 네 mode를 각각 별도 binary로 링크하고 capability와 모든
resource limit이 계약 표와 일치하는지 검사한다.

`arch_ops_tests`는 x86_64, AArch64, RISC-V 64 backend를 각각 링크해 operation
capability, descriptor 결합, ELF machine/canonical address 검증을 확인한다.

`object_graph_lint`는 네 host archive를 직접 열어 mode object가 정확히 하나인지,
UEFI·BIOS·Raspberry Pi object가 들어오지 않았는지 검사한다. 이는 host link graph
증거이며 firmware나 실기기 동작 증거가 아니다.

QStar와 Make의 compile/test 결과는 host build 증거다. UEFI와 Raspberry Pi
freestanding build는 각 frontend가 normal mode source와 신규 service source를 함께
링크할 수 있는지 확인한다. 이 기록은 QEMU kernel boot나 live hardware 성공을
주장하지 않는다.
