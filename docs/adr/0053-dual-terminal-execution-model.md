---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-08-02
code_paths:
  - include/Ribon/boot/image.h
  - include/Ribon/boot/plan.h
  - include/Ribon/protocol/protocol.h
  - src/common/boot.c
  - src/common/protocol.c
  - src/image-formats/
  - src/protocols/os/
tests:
  - make check-loader
  - make check-pe-coff
  - make check-boot-lifecycle
  - make check-protocol-contract
  - make check-os-packages
hardware:
  - none
supersedes:
  - ADR 0011의 direct-entry 단일 terminal suffix
  - ADR 0013의 모든 protocol에 대한 handoff와 register invocation 의무
---

# ADR 0053: Dual terminal execution model

## 결정

Ribon Boot Protocol은 정확히 하나의 terminal execution kind를 선언한다.

- `DIRECT_ENTRY`: Ribon이 image를 직접 배치하고 protocol handoff와 register invocation을 만든 뒤
  architecture backend가 terminal branch를 수행한다.
- `FIRMWARE_MANAGED_IMAGE`: 살아 있는 firmware image service가 검증된 candidate를 로드하고 실행한다.
  Protocol은 native firmware handle이나 callback pointer를 받지 않으며 register invocation을 만들지 않는다.

`RibonValidatedImage`는 executable format, machine, exact candidate byte extent, validation receipt와
허용 execution bit를 소유한다. `RibonDirectLoadPlan`은 caller-owned segment storage와 직접 배치에만
필요한 address fact를 소유한다. Managed transaction에는 direct plan이 없고 direct transaction에는
반드시 하나가 있다. 두 구조를 다시 합치는 compatibility wrapper는 두지 않는다.

Protocol은 `RibonTerminalRequest`를 생성한다. Direct request만 `RibonEntryInvocation`을 포함한다.
Managed request의 direct entry storage는 전부 0이어야 한다. Unknown kind, kind/descriptor 불일치,
managed-with-entry와 direct-without-entry는 protocol preparation에서 fail-closed한다.

## Lifecycle ownership

두 모델은 candidate read, validation, policy decision과 durable attempt commit까지 공통 prefix를 가진다.
그 뒤 suffix의 authority는 분리한다.

```text
DIRECT_ENTRY
  COMMIT_ATTEMPT -> final handoff refresh -> QUIESCE_ENVIRONMENT -> TRANSFER

FIRMWARE_MANAGED_IMAGE
  COMMIT_ATTEMPT -> EXECUTE_TERMINAL(provider) -> child exit or no return
```

Managed provider는 environment-owned typed service여야 하며 Core public ABI에 EFI type을 노출하지 않는다.
Provider 구현은 후속 ADR에서 승인한다. R01에서는 provider가 없으므로 managed transaction이 direct
quiesce 또는 transfer를 시도하면 `EXECUTE_TERMINAL`/`TERMINAL` receipt로 실패하며 environment closure를
호출하지 않는다.

## UEFI 근거

UEFI 2.11은 EFI image를 `LoadImage()`가 memory에 로드·재배치하고 `StartImage()`가 loaded image의
entry로 제어를 넘긴다고 정의한다. EFI image entry는 firmware가 제공하는 image handle과 system
table을 받는다. 따라서 PE/COFF section을 일부 해석한 뒤 일반 architecture register bridge로 분기하는
것은 firmware-managed image 실행과 동등하지 않다. 정본은
<https://uefi.org/specs/UEFI/2.11/04_EFI_System_Table.html>과
<https://uefi.org/specs/UEFI/2.11/>이다.

## 실패 정책

Image parser output의 format, machine, exact size 또는 execution bit가 selected image plugin과 다르면
거부한다. Direct plan의 ABI, segment range, entry coverage와 architecture machine이 다르면 거부한다.
Managed request의 format/machine이 environment provider와 맞지 않거나 provider가 없거나 child가
예상 밖으로 반환하면 terminal failure다. Commit 전 실패는 terminal action을 호출하지 않고, commit 뒤
실패는 durable attempt를 되돌렸다고 주장하지 않는다.

## 비주장

이 결정은 실제 UEFI `LoadImage()`/`StartImage()` provider, Linux EFI stub 실행, FreeBSD 실행,
returned-child cleanup, secure boot 또는 production firmware support를 열지 않는다. FreeBSD package가
유효한 managed requirement를 생성한다는 것은 contract evidence이며 runtime boot evidence가 아니다.
