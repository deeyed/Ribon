---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-08-01
code_paths:
  - include/Ribon/update/installer.h
  - src/update/installer.c
  - src/environments/uefi-app/update_storage.c
  - products/validation/manifests/x86_64-uefi-update-recovery.json
tests:
  - make check-update-installer
  - make check-uefi-update-storage
  - make check-qemu-update-install
hardware:
  - qemu-q35-uefi
supersedes:
  - host-only update writer evidence
---

# ADR 0048: Firmware adapter는 Block I/O만 제공하고 generic installer가 update 의미를 소유한다

## Context

D01과 D02는 signed manifest, deterministic A/B layout, metadata state machine과 semantic writer를
제공했지만 실제 firmware-visible media에서 payload를 설치하지 않았다. Host file provider는 codec과
state machine reference로 유효하나 UEFI Block I/O 실행 evidence를 대신할 수 없다.

UEFI-specific installer를 만들면 manifest authorization, inactive protection, readback과 metadata
transition이 firmware API에 종속된다. 반대로 일반 UEFI loader에 writer를 항상 link하면 normal
제품의 attack surface가 커진다. GPT partition discovery를 generic Core에 넣으면 raw flash와 native
controller product가 UEFI/q35 media 형식에 결속된다.

## Decision

- Generic `src/update/installer.c`가 manifest 재승인, inactive typestate, component digest, semantic
  erase/write, full readback, flush와 `VERIFIED` successor를 소유한다.
- Bundle transport와 storage mechanism은 bounded callback으로만 들어오며 installer는 firmware와
  architecture에 독립적이다.
- UEFI adapter는 Block I/O handle discovery, exact block operation과 flush만 제공한다.
- Product binding의 layout/media digest와 anchor가 exact device identity를 고정한다. 0개 또는 복수
  match는 실패한다.
- GPT는 q35 reference media와 independent host inspection에 사용하지만 target adapter는 GPT를
  update policy authority로 해석하지 않는다.
- Writer adapter와 installer는 recovery product에만 link한다. Normal UEFI product source graph는
  변경하지 않는다.
- QEMU harness는 pristine fixture를 직접 변경하지 않고 results root의 runtime copy만 변경한다.
- Compatibility selector와 host-file-as-runtime fallback을 만들지 않는다.

## Consequences

같은 generic installer를 native flash/controller adapter에도 재사용할 수 있고 firmware별 code는
device discovery와 exact I/O로 제한된다. Product graph의 음성 증거로 normal boot의 writer 부재를
검사할 수 있다. QEMU 재부팅은 host process 내부 state가 아니라 같은 persistent raw disk의
`VERIFIED` metadata를 재개방한다.

Reference UEFI adapter의 metadata 두-copy write와 firmware flush는 physical power-loss atomicity를
증명하지 않는다. D04 이후 transaction journal, protected rollback state, `PENDING`/confirmation과
recovery fallback을 별도 단계로 닫아야 한다. 다른 firmware/board는 자신의 media identity와 adapter
evidence를 제공해야 한다.

## 기각한 대안

### UEFI file을 inactive slot로 취급

Host directory 또는 FAT file write는 block media identity, layout protection과 reboot persistence를
같은 계약으로 입증하지 못하므로 기각한다.

### Generic Core에서 GPT partition name 검색

UEFI/q35-specific description을 native flash와 raw-FDT product에 강제하고 partition name이 update
authority가 되므로 기각한다.

### Normal UEFI binary에서 runtime recovery flag 사용

Writer와 Block I/O mutation code가 normal final image에 남으므로 product-scoped static composition을
선택한다.

### Fixture raw disk를 직접 변경

반복 실행이 initial state에 의존하고 provenance의 pristine digest가 사라지므로 results root의
runtime copy를 사용한다.
