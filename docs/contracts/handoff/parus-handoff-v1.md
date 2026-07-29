---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-29
code_paths:
  - src/protocols/os/parus/
  - include/Ribon/protocol/
  - ../../../../sys/include/parus/boot/
  - ../../../../sys/kern/boot/
tests:
  - ribon-parus-handoff-v1-unit
  - parus-ribon-handoff-v1-consumer
  - ribon-parus-handoff-v1-malformed
hardware:
  - none
supersedes:
  - legacy previous handoff artifact
---

# Parus Handoff v1 계약

Parus Handoff v1은 Ribon의 Parus Boot Protocol이 생성하고 Parus `xibalba()` 경계가 소비하는
wire artifact다. Ribon Core는 이 형식을 알지 않는다.

## Wire 규칙

- Artifact 이름은 `RPH1`이다.
- 모든 integer는 little-endian으로 byte-wise 직렬화한다.
- C struct를 wire buffer에 직접 cast하거나 packed struct로 기록하지 않는다.
- Header 크기는 64 byte다.
- Section entry 크기는 32 byte다.
- Section payload 시작은 16 byte alignment를 만족한다.
- Artifact 전체 크기는 65,536 byte를 넘지 않는다.
- Section 수는 32개를 넘지 않는다.
- 모든 reserved field는 producer가 0으로 기록하고 consumer가 0인지 검증한다.

## Header

| Offset | 크기 | Field | 값 또는 의미 |
| ---: | ---: | --- | --- |
| 0 | 4 | `magic` | ASCII `RPH1` |
| 4 | 2 | `version_major` | `1` |
| 6 | 2 | `version_minor` | `0` |
| 8 | 2 | `header_size` | `64` |
| 10 | 2 | `section_entry_size` | `32` |
| 12 | 2 | `section_count` | 최대 32 |
| 14 | 2 | `reserved0` | `0` |
| 16 | 4 | `total_size` | header를 포함한 artifact 크기 |
| 20 | 4 | `section_table_offset` | 64 이상, 16 byte aligned |
| 24 | 8 | `flags` | artifact capability bitset |
| 32 | 4 | `crc32c` | CRC-32C |
| 36 | 4 | `reserved1` | `0` |
| 40 | 8 | `boot_generation` | slot metadata generation |
| 48 | 8 | `manifest_sequence` | anti-rollback manifest sequence |
| 56 | 8 | `reserved2` | `0` |

CRC-32C는 `crc32c` field를 0으로 놓고 `total_size` 전체에 대해 계산한다. CRC는 메모리
손상 검출용이며 manifest signature를 대신하지 않는다.

## Section entry

| Offset | 크기 | Field | 의미 |
| ---: | ---: | --- | --- |
| 0 | 4 | `type` | section type |
| 4 | 4 | `flags` | section 이해 의무와 lifetime |
| 8 | 8 | `offset` | artifact 시작 기준 payload offset |
| 16 | 8 | `length` | payload byte 수 |
| 24 | 4 | `alignment` | producer가 보장한 alignment |
| 28 | 4 | `reserved` | `0` |

Section flag bit 0은 `REQUIRED_TO_UNDERSTAND`, bit 1은 `BORROWED_RANGE_DESCRIPTOR`다.
알 수 없는 required section은 artifact 전체를 거부한다. 알 수 없는 optional section은
bounds와 overlap을 검증한 뒤 건너뛸 수 있다.

Section table과 payload는 서로 겹치지 않는다. 두 payload도 겹치지 않는다. `offset +
length`의 overflow, `total_size` 초과, 선언 alignment 위반은 artifact 실패다.

## Section type

| Type | 이름 | 수량 | 요구 |
| ---: | --- | ---: | --- |
| `0x0001` | `MEMORY_MAP` | 1 | 필수 |
| `0x0002` | `RESERVED_RANGES` | 1 | 필수 |
| `0x0003` | `KERNEL_IMAGE_LAYOUT` | 1 | 필수 |
| `0x0004` | `DEVICE_TREE` | 0 또는 1 | AArch64/RPi 계열 필수 |
| `0x0005` | `ACPI_RSDP` | 0 또는 1 | ACPI platform 선택 |
| `0x0006` | `COMMAND_LINE` | 0 또는 1 | 선택 |
| `0x0007` | `FRAMEBUFFER` | 0 또는 1 | 선택 |
| `0x0008` | `BOOT_MODULES` | 0 또는 1 | 선택 |
| `0x0009` | `BOOT_MEDIA` | 0 또는 1 | 선택 |
| `0x000a` | `BOOT_PROVENANCE` | 1 | 필수 |
| `0x000b` | `OVERSEER_STATE` | 0 또는 1 | 선택 |

복수 item은 하나의 section payload 안에서 `count`, `entry_size`, fixed-size entry table로
직렬화한다. Entry 수와 크기에 각 section 계약의 상한을 둔다.

`DEVICE_TREE`, framebuffer memory, module payload처럼 큰 외부 blob은 artifact 안에 복사하지
않는다. Physical address, size, protection class, digest를 가진 borrowed-range descriptor로
표현한다. Consumer는 EB2 이전에 범위를 검증하고 보호 range에 등록한다.

## Section payload

### Memory와 reserved range

`MEMORY_MAP`과 `RESERVED_RANGES` payload는 같은 형식을 사용한다.

| Offset | 크기 | Field |
| ---: | ---: | --- |
| 0 | 4 | `count` |
| 4 | 4 | `entry_size`, 값 `32` |
| 8 | `count * 32` | entry table |

각 entry는 `base` u64, `length` u64, `kind` u32, `reserved` u32, `attributes` u64
순서다. `reserved`는 0이다. `MEMORY_MAP`은 정규화된 전체 map이고
`RESERVED_RANGES`는 `USABLE` 이외의 범위만 포함한다.

### Kernel image layout

고정 header는 128 byte다. Offset 0의 `layout_version` u32는 1, offset 4의
`segment_count` u32는 최대 16이다. 이후 u64 field는 다음 순서다.

| Offset | Field |
| ---: | --- |
| 8 | `entry_virtual` |
| 16 | `entry_load` |
| 24 | `entry_runtime` |
| 32 | `load_base` |
| 40 | `load_end` |
| 48 | `runtime_load_base` |
| 56 | `runtime_load_end` |
| 64 | `linked_virtual_base` |
| 72 | `linked_virtual_end` |
| 80 | `linked_physical_base` |
| 88 | `linked_physical_end` |
| 96 | `high_entry_virtual` |
| 104 | `high_entry_load` |
| 112 | `image_memory_size` |

Offset 120은 `load_plan_flags` u32, offset 124는 0인 reserved u32다. 각 segment
entry는 64 byte이며 `virtual`, `linked_physical`, `load`, `runtime`,
`memory_size`, `file_size`, `alignment`, `flags` u64 순서다.

### Borrowed firmware descriptor

`DEVICE_TREE`와 `ACPI_RSDP`는 32-byte descriptor를 사용한다. Offset 0은 physical
address u64, offset 8은 size u64, offset 16과 20은 type별 metadata u32, offset
24는 0인 reserved u64다. Section flag에 `BORROWED_RANGE_DESCRIPTOR`를 설정한다.
ACPI metadata0은 RSDP revision이다.

### Command line

`COMMAND_LINE`은 artifact 안에 복사한 NUL-terminated byte string이다. Empty payload와
마지막 NUL이 없는 payload는 거부한다. Payload 상한은 종단 NUL을 포함한 4,096 byte다.
Producer는 이 상한을 넘는 environment command line을 artifact 생성 전에 거부하고,
producer-side parser와 Parus consumer도 같은 상한을 독립적으로 검증한다.

### Framebuffer

`FRAMEBUFFER` descriptor는 48 byte다. Physical address u64 뒤에 width, height,
pitch, bits-per-pixel, backend u32가 오고, offset 28부터 red/green/blue position과
mask size를 각각 u8로 기록한다. 나머지 byte는 0이다.

### Module, boot media, provenance

`BOOT_MODULES`는 offset 0의 `count` u32, offset 4의 `entry_size` u32 값 32와
32-byte entry table을 사용한다. Entry는 physical address u64, size u64, flags
u32, reserved u32, name-digest 예약 u64 순서다.

이 section은 선택 사항이지만 존재하면 `count`는 1 이상 8 이하여야 한다. Section
flag는 `REQUIRED_TO_UNDERSTAND | BORROWED_RANGE_DESCRIPTOR`를 함께 설정한다. Entry
`flags` 값 0은 auxiliary module, bit 0은 initial image이며 그 밖의 bit는 거부한다.
한 artifact에는 initial image가 최대 하나만 존재한다. Address와 size는 0일 수 없고
합 overflow, module 상호 overlap, kernel image physical span과의 overlap은 실패다.
`reserved`와 예약 name digest는 v1.0에서 반드시 0이다.

`BOOT_MEDIA`는 32 byte다. Kind u32, block size u32, physical address u64, size
u64, reserved u64 순서다.

`BOOT_PROVENANCE`는 32 byte다. Firmware ID, architecture ID, Ribon major, minor,
patch, reserved를 u32로 기록하고 마지막 u64은 provenance flag 예약 영역이다.
Architecture ID는 AMD64 1, AArch64 2, RISC-V 64 3이다.

`OVERSEER_STATE` type과 singleton 규칙은 예약한다. 별도 계약이 payload schema를
정의하기 전 producer는 이 section을 생성해서는 안 된다.

## Parser 의무

Producer는 artifact를 반환하기 전에 같은 bounded parser로 자체 검증한다. Consumer도
magic, version, header size, reserved field, CRC32C, table bounds, payload alignment,
payload overlap, singleton duplication, required section 존재, section별 payload shape를
독립적으로 검증한다. 검증 실패를 다른 handoff format으로 재시도하지 않는다.

## Entry register

| Architecture | Handoff pointer | Entry flags |
| --- | --- | --- |
| AArch64 | `x0` | `x1` |
| AMD64 | `rdi` | `rsi` |
| RISC-V 64 | `a0` | `a1` |

Entry flag bit 0은 `RPH1`, bit 1은 direct-DTB 예약, bit 2는 `ENTERED_HIGH`, bit 3은
`DIRECT_HIGH`다. Normal Parus protocol은 bit 0만 설정한다. Direct-high mode는 bit 0, 2,
3을 함께 설정한다.

Flag는 pointer와 CPU state 검증을 대체하지 않는다. Malformed RPH1을 DTB 또는 다른
artifact로 재해석하지 않는다.

## 수명

RPH1 raw pointer와 section table은 Parus EB0-EB2에서만 직접 소비한다. Runtime은 raw
artifact를 재해석하지 않고 검증된 descriptor와 immutable boot constants만 사용한다.

## 보안 경계

RPH1은 signed manifest의 검증 결과를 전달하지만 그 자체가 서명 envelope는 아니다.
Consumer는 CRC, bounds, section 관계, physical range를 독립적으로 검증한다. Manifest
sequence가 platform anti-rollback state보다 작으면 부팅을 거부한다.
