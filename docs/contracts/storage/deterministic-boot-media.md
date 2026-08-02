---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-27
code_paths:
  - include/Ribon/storage/block.h
  - include/Ribon/filesystem/fat32.h
  - include/Ribon/config/boot_config.h
  - src/storage/
  - src/filesystems/
  - src/config/
  - src/environments/uefi-app/
tests:
  - make check-media-pipeline
  - make check-normal-media-surface
  - make x86_64-uefi-parus-fixture-smoke
  - make check-uefi-product-hermeticity
hardware:
  - none
supersedes:
  - embedded UEFI payload boot source
---

# 결정론적 Boot Media와 Configuration 계약

Boot media pipeline은 read-only block range, partition view, filesystem reader,
configuration candidate, selected boot source를 독립된 경계로 유지한다. Update writer와
network transport는 normal boot media reader의 authority가 아니다.

## Read-only block provider

`RibonReadOnlyBlockDevice`는 logical block size, total block count, one-call read upper bound,
environment-private context와 exact read callback을 선언한다. Callback은 요청 byte를 전부
읽은 경우에만 성공한다. Partial read, range overflow, zero block count와 native controller
handle 노출은 허용하지 않는다.

`deadline_ticks`는 environment가 해석하는 absolute time이다. Parser-local metadata read는
0을 전달할 수 있지만 boot transaction의 source callback은 selected monotonic authority가
정한 deadline을 전달한다.

## GPT와 protective MBR

GPT parser는 LBA 0 protective MBR, primary header, partition entry array를 같은 bounded
input에서 byte-wise로 검증한다.

- protective MBR signature와 exactly one `0xEE` entry를 요구한다.
- header signature, revision, header size, header CRC32, current/backup LBA, usable LBA range,
  entry count와 entry size를 검증한다.
- entry array offset/count multiplication overflow와 table CRC32를 검증한다.
- non-empty partition의 inclusive LBA range는 usable range 밖으로 나가거나 서로 겹치면 안 된다.
- parser는 repair, backup-header fallback, writable metadata 갱신을 수행하지 않는다.

GPT CRC32는 RPH1의 CRC32C와 다른 on-media integrity field다. 어느 CRC도 signed manifest,
anti-rollback 또는 bundle authenticity를 대신하지 않는다.

## FAT32 read-only subset

FAT32 reader는 validated `RibonReadOnlyBlockDevice` 위에서 동작한다. BPB, FAT area,
data-cluster geometry, root cluster와 cluster-count를 검증하고 caller-owned one-sector scratch만
사용한다.

- FAT12/16, long file name, ext4, NTFS, APFS와 ZFS는 이 contract의 reader가 아니다.
- path는 absolute, canonical, root-contained ASCII 8.3 component만 허용한다.
- empty component, `.`/`..`, trailing separator, backslash, control byte, depth와 component-length
  초과는 거부한다.
- directory와 file cluster chain은 validated data range에 있어야 하며, EOC 전 short chain,
  bad cluster와 bounded traversal limit 초과는 failure다.
- write, erase, repair, allocation, directory mutation과 update staging은 제공하지 않는다.

## Configuration grammar

Configuration byte stream은 ASCII `version=1`과 하나 이상의 closed candidate block을 가진다.

```text
version=1
entry=primary
priority=100
protocol=parus
image=elf64
kernel=/RIBON/PAYLOAD.ELF
cmdline=console=ttyS0
init_image=/RIBON/INIT.IMG
module=/RIBON/EXTRA.IMG
end
```

각 candidate는 `priority`, `protocol`, `image`, `kernel`을 정확히 한 번 가져야 한다.
`cmdline`과 `init_image`는 선택 singleton이고 `module`은 auxiliary module의 bounded
repeat field다. Initial image를 포함한 총 module 수는 8개 이하다. Unknown key,
duplicate singleton key, incomplete block, non-canonical path, count/length overflow는
fail-closed다. Highest priority candidate만 선택하며 same-priority tie는 선택하지 않는다.

Configuration은 Boot Protocol ID와 image-format ID를 명시한다. Target은 selected product graph가
그 조합을 지원하지 않거나 protocol이 module component를 인수하지 못하면 candidate를 무시하지
않고 실패해야 한다. 한 candidate의 format/protocol failure를 다른 candidate나 protocol로 자동
재해석하지 않는다.

## UEFI consumer

UEFI consumer는 loaded-image device의 Simple File System을 capture하여 canonical path file을
read-only `RibonBootSource` slot으로 변환한다. `EFI_FILE_PROTOCOL`, root handle, loaded-image
handle과 Block I/O handle은 environment-private context에만 남는다. File size는 seek/query로
검증하고 source read는 exact seek/read만 성공으로 처리한다.

UEFI source provider의 64 MiB input budget은 product-wide source-service 계약이다. x86_64
consumer target은 그와 독립적으로 8 MiB static payload buffer를 가지며, 선택한 file이 그
target-local bound를 넘으면 transfer 전에 거부한다.

UEFI target은 선택된 `init_image`와 `module` file을 exact-size
`EfiLoaderData` page allocation에 읽고 firmware-neutral typed module inventory로
낮춘다. File open, size, bounded page allocation, exact read 중 하나라도 실패하면
handoff 전에 종료한다. Boot media, command line과 module inventory는 persistent
semantic input으로 묶어 최초 environment capture와 모든 final memory-map recapture
뒤 동일하게 다시 적용한다.

UEFI Block I/O도 optional typed `RibonReadOnlyBlockDevice`로 변환할 수 있다. Generic storage
library는 EFI type이나 GUID를 include하지 않는다. ExitBootServices 이후에는 file, block,
timer와 다른 Boot Services callback을 다시 호출하지 않는다.

## Product와 object graph

x86_64 UEFI boot manager의 ESP recipe는 application, configuration, external payload file을
서로 분리한다. Runtime application object graph에는 build-embedded payload object가 없어야 한다.
Raw-FDT memory source는 별도 fixture/product 경계이며 UEFI file source의 compatibility alias가
아니다.

Fixture, external-kernel과 Linux EFI UEFI product는 각각 독립 product ID, generated registry,
object, link map, ESP, copied manifest와 result root를 가진다. External payload selector가 fixture
product의 dependency나 output identity를 바꾸지 않으며 어떤 product도 공통 writable ESP를
사용하지 않는다. External product는 payload copy 전에 manifest tuple, ELF class/window와
payload digest를 검증한다. Fixture marker를 포함한 input은 external product에서 거부한다.

Linux EFI product도 별도 registry와 launcher object를 소유한다. Selected source의 canonical path와
loaded-image device identity로 full device path를 만들며, exact validated source buffer만 firmware
`LoadImage()`에 전달한다. Direct-entry UEFI products에는 launcher symbol이 링크되지 않는다.

Normal product manifest와 final object graph에는 inactive-slot writer 또는 network transport
authority가 없어야 한다. Recovery/network/update writer product는 별도 mode graph가 명시적으로
선택할 때만 허용된다.

## Update media와의 분리

Read-only boot media pipeline은 update layout을 발견하거나 writer로 승격하지 않는다. Recovery와
provisioning product의 writable media는 {doc}`bounded-update-slot-provider-v1`의 deterministic
layout, semantic inactive-slot handle과 explicit provider binding을 사용한다. 같은 native device를
사용하더라도 boot-source reader와 inactive writer는 product graph에서 서로 다른 typed service ID다.
