---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-08-01
code_paths:
  - include/Ribon/update/storage.h
  - src/update/storage.c
  - tools/update_layout.py
  - tools/generate_plugin_registry.py
tests:
  - make check-update-storage
  - make check-update-storage-sanitizer
  - make check-update-storage-cross-compile
  - make check-update-storage-graphs
hardware:
  - none
supersedes:
  - raw inactive-slot storage callback
---

# ADR 0047: Update writer는 deterministic layout과 semantic inactive-slot handle을 사용한다

## Context

D01은 signed update manifest와 component maximum range를 고정했지만 payload를 놓을 media layout과
slot lifecycle은 소유하지 않는다. Service directory의 raw slot callback만 사용하면 product가
서로 다른 offset을 같은 slot 의미로 해석하거나 active slot을 writer에게 전달할 수 있다.
Native partition library나 filesystem을 update Core에 넣으면 board/firmware API와 dynamic media
policy가 generic boundary에 유입된다.

Normal boot는 작은 read path만 가져야 한다. 같은 binary에서 mode flag로 writer를 열면 reachable
attack surface와 capability graph가 normal evidence에 남는다. Layout metadata를 native C struct로
저장하면 endian, padding과 compiler ABI에 따라 recovery identity가 달라진다.

## Decision

- Storage mechanism은 capacity, alignment, transfer 상한과 exact read/write/erase/flush callback으로
  구성한 generic provider ABI다.
- A/B layout은 source-neutral scalar에서 11개 canonical range와 512-byte identity를 계산한다.
- Slot metadata는 exact 512-byte little-endian codec, SHA-256과 CRC32C를 사용한다.
- Metadata generation과 image generation은 독립이며 wrap하지 않는다.
- Core가 inactive `STAGING` slot을 확인한 뒤 generation과 media/layout digest를 semantic handle에
  결속한다. Policy는 raw media address를 받지 않는다.
- Active confirmed slot은 transition, write와 erase 대상이 아니다.
- Product composer는 writer service와 capability를 `update_storage` recovery/provisioning binding에만
  허용한다. Normal graph의 writer reachability는 hard error다.
- Test-only memory/file provider는 behavior reference일 뿐 production driver class가 아니다.
- Compatibility wrapper, old/new layout selector와 implicit partition discovery를 두지 않는다.

## Consequences

Manifest maximum range, media layout, metadata와 writer reachability가 서로 다른 independent gate에서
같은 layout digest로 닫힌다. Board provider는 native I/O만 구현하고 generic update engine은 active
slot 보호와 exact range 계산을 재사용할 수 있다. Recovery와 provisioning image만 writer code를
link하므로 normal boot attack surface가 증가하지 않는다.

이 결정만으로 update transaction 전체가 완성되지는 않는다. Redundant metadata commit order,
manifest authorization sequence, readback digest, protected rollback transition, network transport와
OS confirmation은 후속 contract가 조합한다. Physical media durability는 provider별 evidence가 필요하다.

## 기각한 대안

### GPT partition name을 Core에서 검색

GPT는 한 media description일 뿐 firmware volume, raw flash와 fixed ROM layout을 포괄하지 못한다.
Partition parser가 update authority를 소유하게 되므로 기각한다.

### Policy에 raw offset write helper 제공

Verifier가 semantic active-slot invariant를 증명할 수 없고 board layout을 language schema에 고정하므로
기각한다.

### Normal binary에서 runtime flag로 writer 활성화

Writer callback과 native driver가 normal final graph에 남아 capability denial만으로 attack surface를
제거할 수 없으므로 기각한다.

### Metadata를 packed C struct로 저장

Cross-architecture identity, torn-write parser와 independent host inspection을 안정적으로 설명할 수
없으므로 explicit LE codec를 선택한다.
