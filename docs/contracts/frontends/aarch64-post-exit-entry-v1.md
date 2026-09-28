---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-09-28
code_paths:
  - include/Ribon/arch/aarch64/entry_state.h
  - src/arch/aarch64/entry_state.c
  - src/arch/aarch64/transition.S
  - targets/uefi-app/entry.c
tests:
  - make check-arch-aarch64
  - make check-uefi-exit-transaction
  - make aarch64-uefi-parus-fixture
hardware:
  - qemu-virt-aarch64-edk2
supersedes:
  - broad low-512-GiB post-exit identity table
---

# AArch64 post-exit entry v1

이 계약은 UEFI 호출 가능 구간과 Ribon-owned post-exit bridge, LUCA-owned 영구 pmap의
경계를 정의한다. `RibonAarch64EntryState`와 `RibonAarch64PostExitTransition`은 ABI v1의
Ribon 내부 schema다. LUCA public handoff에는 table pointer나 firmware register를 넣지
않으며 완료 사실만 `EL1_NORMALIZED` flag로 전달한다.

## 상태와 순서

Ribon은 `CurrentEL`을 먼저 읽고 EL1에서는 EL1 register bank, EL2에서는 EL2 register
bank만 관측한다. Handoff pointer, loaded image, payload source와 destination, stack,
module, DTB, bridge table 및 port가 선언한 MMIO는 현재 stage-1에서 identity-addressed인지
검사한다. Range는 page alignment, nonzero length, overflow, 512 GiB 상한, 정렬된 순서와
서로 다른 memory kind의 overlap을 검증한다.

Bridge는 root 한 page와 사용한 1 GiB 구간별 L2 table 최대 8 page를 사용한다. 등록된
range가 포함된 2 MiB block만 normal 또는 device attribute로 게시한다. 이 v1 granularity
때문에 같은 2 MiB block에 상충하는 normal/device range가 있으면 실패한다. Table 용량을
늘리거나 전체 저주소를 mapping해 성공으로 바꾸지 않는다.

`ExitBootServices()` 전에는 firmware stack, active translation과 firmware 호출 가능성을
보존한다. Map-key가 바뀌면 firmware 상태에서 map을 갱신하고 재시도한다. 실패하면
bridge, stack과 EL을 바꾸지 않는다. 성공 callback 뒤에는 firmware service를 호출하지
않으며 assembly가 bridge와 caller stack을 설치한다. EL2 source는 VHE를 유지하지 않고
`HCR_EL2.RW`, EL1 timer access, zero counter offset와 EL1h SPSR을 설정한 뒤 `eret`한다.

## FreeBSD provenance와 제외 항목

`src/arch/aarch64/transition.S`의 CurrentEL dispatch, barrier 순서, EL2 timer/trap
정규화와 EL1h return ordering은 FreeBSD
`sys/arm64/arm64/locore.S` revision
`9f5c4ef32812afb4573a278e6eafe5040f839d13`를 참고했다. 분석한 원본 SHA-256은
`f8455440b5e72fe8430a6ee709f454c7e40cfa0e90a88461af23bb1f54dc32b6`이다. 직접 적응한
assembly 파일에는 Andrew Turner의 BSD-2-Clause 원문을 보존한다.

FreeBSD의 VHE stay-at-EL2, 커널 전체 page-table/bootstrap, KASLR, SMP와 platform
discovery는 가져오지 않았다. Ribon은 LUCA의 EL1 runtime 계약에 필요한 최소 전환만
소유한다. Linux와 Zircon 구현은 이 파일에 이식하지 않았다.

## 증거 경계

Host model은 EL1/EL2 register 선택, malformed range와 table shape를 검증한다. PE/COFF
build는 assembly의 ARM64 link evidence다. 실제 EDK2/QEMU product smoke에서
`ExitBootServices`, Ribon bridge source EL, LUCA `ENTRY_EL1=OK`와 kernel runtime을 함께
관측해야 결합 실행을 주장할 수 있다. 이 계약은 UTM, Secure Boot와 physical board의
실행 증거가 아니다.
