---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-08-01
code_paths:
  - include/Ribon/update/transaction.h
  - src/update/transaction.c
  - src/update/installer.c
  - tools/qemu_update_power_cut.py
tests:
  - make check-update-power-cut
  - make check-qemu-update-power-cut
hardware:
  - qemu-q35-uefi
supersedes:
  - duplicate metadata as update commit authority
---

# ADR 0049: Update install은 selector-committed journal과 reopen 기반 resume를 사용한다

## Context

D03의 generic installer는 signed component write, full readback과 `VERIFIED` successor를 만들었지만
metadata commit 순서와 모든 interruption 이후 resume를 하나의 authority로 닫지 않았다. 단순한
두 metadata copy는 어느 write가 commit point인지, newest copy가 손상됐을 때 이전 copy로 내려가도
되는지, payload flush와 state transition 사이 crash를 어떻게 분류할지 설명하지 못한다.

Firmware별 transaction을 만들면 UEFI, raw-FDT와 native flash product가 서로 다른 update 의미를
갖는다. 반대로 generic Core가 GPT, EFI file 또는 controller cache를 해석하면 mechanism boundary가
무너진다.

## Decision

- Generic coordinator가 `STAGING -> payload flush -> VERIFIED -> PENDING` 순서를 소유한다.
- Update journal은 2개의 1024-byte complete record와 2개의 512-byte selector를 explicit LE wire로
  저장한다.
- Selector flush가 generation commit point다. Record는 full readback 뒤에만 selector로 공개한다.
- Newest selector가 가리키는 record가 손상되면 older selector로 downgrade하지 않는다.
- Record와 selector는 predecessor generation/digest, canonical metadata, manifest, image-set과 layout
  identity를 결속한다.
- Coordinator는 매 retry에서 signed manifest를 다시 승인하고 durable journal을 reopen한다.
- 같은 identity의 STAGING, VERIFIED와 PENDING만 resume하며 PENDING retry는 generation-idempotent다.
- Stable before/after event ABI로 모든 storage boundary에 deterministic fail-stop을 주입한다.
- Normal UEFI graph에는 writer, installer와 transaction source를 link하지 않는다.
- PENDING은 protected rollback-state trial commit 전에는 boot authority가 아니다.

## Consequences

Architecture, firmware와 OS에 독립적인 crash-consistency state machine을 native storage provider에
재사용할 수 있다. Host test는 bounded trace 전체를 전수 실행하고 QEMU는 같은 persistent disk의
selected crash snapshot을 실제 recovery product로 재개방한다.

두 record slot 때문에 current commit은 두 generation 전 record를 덮어쓸 수 있다. 따라서 chain의
모든 ancestor object를 보존하지 않고 newest record에 직전 digest를 결속한다. 전체 media rollback은
내부 journal만으로 탐지할 수 없으므로 product가 external minimum generation을 제공해야 한다.

Reference flush model과 OVMF/QEMU evidence는 physical power loss, hardware cache honesty, flash wear,
production anti-replay 또는 실제 보드 성공을 증명하지 않는다.

## 기각한 대안

### 두 metadata copy에서 가장 큰 generation 선택

Newest object가 손상됐을 때 silent downgrade가 가능하고 component write/flush ordering을 결속하지
못하므로 기각한다.

### Payload와 metadata를 하나의 큰 record로 저장

Slot capacity만큼 journal이 커지고 native flash write 특성을 generic ABI에 노출하므로 기각한다.

### Firmware callback 하나에 transaction 위임

UEFI, board flash와 test provider가 서로 다른 lifecycle/retry 의미를 구현하게 되므로 기각한다.

### PENDING commit 즉시 boot

Rollback floor와 trial-attempt durability가 열려 있어 update journal이 security authority를 침범하므로
기각한다.
