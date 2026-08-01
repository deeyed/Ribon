---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-08-01
code_paths:
  - include/Ribon/update/storage.h
  - src/update/storage.c
  - tools/update_layout.py
  - products/validation/manifests/update-storage-reference.json
tests:
  - make check-update-storage
  - make check-update-storage-sanitizer
  - make check-update-storage-cross-compile
  - make check-update-storage-graphs
hardware:
  - none
supersedes:
  - implicit inactive-slot byte writer
---

# Bounded Update Slot Provider v1 계약

Update storage v1은 native media controller와 policy 사이에 deterministic A/B layout,
explicit little-endian metadata, semantic inactive-slot handle과 exact bounded I/O를 둔다.
Provider는 media mechanism만 구현하며 active slot 선택, manifest 승인, rollback 또는 boot
confirmation을 결정하지 않는다.

## 권한 경계

`RibonUpdateStorageProvider`는 다음 네 operation을 완전한 집합으로 제공한다.

| Operation | 의미 | 성공 조건 |
| --- | --- | --- |
| read | media-relative exact read | transferred가 요청 크기와 같음 |
| write | Core가 승인한 range의 exact write | transferred가 요청 크기와 같음 |
| erase | aligned range erase | callback 성공 |
| flush | durability barrier | callback 성공 |

Capacity, read/write/erase alignment와 one-call transfer 상한은 non-zero scalar다. Alignment는
power of two이며 모든 덧셈과 align-up은 u64 overflow를 검사한다. Native file path, EFI handle,
controller register, block address와 product slot offset은 provider의 opaque `context` 뒤에만 있다.

Provider callback은 active-slot 정책을 판정하지 않는다. Core는 callback 전에 session,
layout identity, metadata generation, inactive slot과 `STAGING` typestate를 확인한다. Short I/O는
성공으로 승격하지 않는다.

## Canonical A/B layout

Layout calculator는 exact media capacity와 한 allocation alignment에서 다음 11개 half-open
range를 순서대로 만든다.

```text
bootloader
guard-boot-recovery
immutable-recovery
guard-recovery-slot-a
slot-a
guard-slot-a-slot-b
slot-b
guard-slot-b-metadata
slot-metadata
update-journal
trailing-reserved
```

모든 range는 non-zero, aligned, adjacent, non-overlapping이다. Slot A와 B의 길이는 같고 metadata
region은 512-byte metadata object 두 개 이상을 수용한다. 마지막 reserve는 선언된 minimum 이상이며
layout 전체가 media capacity를 정확히 소비한다. Bootloader와 immutable recovery range는 inactive
slot writer handle로 열리지 않는다.

Layout identity는 exact 512-byte little-endian object다. 128-byte header 뒤에 16개의
24-byte directory slot을 두며 v1은 앞 11개만 사용하고 나머지를 0으로 둔다. 각 row는
`kind:u32`, `flags:u32`, `offset:u64`, `length:u64`다. Identity SHA-256은 product graph의
`layout_digest_sha256`, slot metadata와 session이 공유한다.

D01 update manifest의 각 component에 대해 `bundle_offset + maximum_size`를 검사하고 가장 큰
끝 offset을 layout alignment로 올린 값이 두 image slot에 모두 들어가야 한다. Exact payload
size만으로 slot capacity를 축소하지 않는다.

## Slot metadata wire

Metadata object는 exact 512-byte little-endian wire다. Packed C struct dump가 아니다.

| Offset | 길이 | 내용 |
| ---: | ---: | --- |
| 0 | 32 | `RIBON-SLOT-METADATA-V1` magic |
| 32 | 32 | version, header/total size, generation, active/pending/count/flags |
| 64 | 160 | slot A entry |
| 224 | 160 | slot B entry |
| 384 | 32 | bytes 0..383의 SHA-256 |
| 416 | 4 | bytes 0..415의 CRC32C |
| 420 | 92 | zero reserved |

한 slot entry는 slot ID, state, 독립 metadata/image generation, manifest digest, image-set digest,
layout digest, boot-attempt 상한과 zero reserved를 가진다. `EMPTY` entry는 generation, digest와
attempt가 모두 0이다. 다른 state는 non-zero identity를 가지며 entry generation은 global metadata
generation을 넘지 않는다. `PENDING`만 1..32 boot attempts를 가지며 다른 state의 attempt는 0이다.

Active slot은 정확히 하나의 `CONFIRMED` slot을 가리킨다. Pending marker는 없거나 active와 다른
정확히 하나의 `PENDING` slot을 가리킨다. Reader는 encoded offset을 신뢰하지 않고 exact size,
reserved bytes, SHA-256, CRC32C와 모든 lifecycle invariant를 다시 유도한다.

## Lifecycle

허용된 inactive-slot edge는 다음과 같다.

```text
EMPTY | BAD | inactive CONFIRMED -> STAGING
STAGING -> VERIFIED | BAD
VERIFIED -> PENDING | BAD
PENDING -> CONFIRMED | BAD
```

각 edge는 global metadata generation을 정확히 1 증가시킨다. Generation이 max u64이면 successor를
만들지 않는다. `STAGING` 진입은 새 image generation과 세 digest를 결속한다. 이후 edge는 같은
identity를 요구한다. `PENDING` 진입은 singleton marker와 bounded attempt budget을 함께 기록한다.
`CONFIRMED` 진입은 active marker를 대상 slot로 바꾸고 pending marker를 지운다.

Active confirmed slot은 transition과 write/erase handle 대상으로 선택할 수 없다. Active slot이
바뀐 뒤 이전 confirmed slot만 새 `STAGING` generation으로 재사용할 수 있다.

## Semantic handle과 exact I/O

`RibonUpdateSlotHandle`은 slot ID, metadata generation, media identity digest와 layout digest를
결속한다. Raw offset과 native device handle을 Ribos 또는 policy에 전달하지 않는다. Handle은
생성한 session의 inactive `STAGING` slot에서만 유효하고 metadata generation이 바뀌면 stale이다.

Read/write/erase는 slot-relative offset을 받으며 Core가 canonical slot region에 media offset으로
낮춘다. Zero-size, misalignment, slot bound 초과, u64 wrap, transfer 상한 초과, stale identity와
short callback result는 fail-closed다. Flush는 session 전체 identity를 다시 검증한 뒤 명시적으로
호출한다. Metadata write ordering과 redundant selector commit은 protected-state 또는 update
transaction 계층이 소유한다.

## Product graph

Writer는 `update_storage` binding을 가진 recovery 또는 provisioning bootloader product에만 있다.
Binding은 layout identity, provider class와 다음 네 서로 다른 service ID를 고정한다.

- read-only boot source
- inactive-slot storage writer
- persistent metadata
- storage flush

Composer는 typed service role과 required/allowed capability의 완전한 일치를 검사한다.
`update_storage` 없이 inactive writer service나 `INACTIVE_SLOT_WRITE/ERASE` capability를 선언하면
composition이 실패한다. Normal product에는 binding, writer service와 두 writer capability가 모두
없어야 한다. Reference product는 graph와 codec 검증용이며 production media provider가 아니다.

## 검증과 evidence 한계

Host unit은 deterministic layout, manifest maximum projection, metadata corruption/torn write,
state edge, active-slot protection, alignment/range, short I/O와 memory/file reference provider를
검사한다. Python inspector는 C가 만든 layout identity와 metadata wire를 독립적으로 비교한다.
Sanitizer와 x86_64/AArch64/RISC-V freestanding compile gate가 같은 source를 검사한다.

이 계약은 UEFI Block I/O driver, board flash driver, wear leveling, physical power-loss atomicity,
production secure boot, network OTA, RPi5 live hardware와 OS confirmation 성공을 주장하지 않는다.
