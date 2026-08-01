---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-08-01
code_paths:
  - include/Ribon/update/installer.h
  - include/Ribon/update/transaction.h
  - src/update/installer.c
  - src/update/transaction.c
  - src/environments/uefi-app/update_storage.h
  - src/environments/uefi-app/update_storage.c
  - targets/x86_64-uefi-update-recovery/entry.c
  - tools/make_qemu_update_fixture.py
  - tools/inspect_qemu_update_disk.py
  - tools/qemu_update_install.py
tests:
  - make check-update-installer
  - make check-uefi-update-storage
  - make check-qemu-update-install
  - make check-update-power-cut
  - qstar --file qstar.lua check
hardware:
  - qemu-q35-uefi
supersedes:
  - host-file-only update installation
---

# Signed Bundle Install v1 계약

Signed bundle installer v1은 승인된 update manifest의 component를 inactive slot에 exact write하고,
전체 component를 다시 읽어 digest를 확인한 뒤에만 `VERIFIED` metadata successor를 만든다. Generic
installer는 architecture, firmware, block device, filesystem과 OS 이름을 알지 않는다. Product가
선택한 provider와 bundle source만 mechanism을 제공한다.

## Generic installer 입력

`RibonUpdateInstallRequest`는 다음 authority와 storage를 한 번의 bounded transaction에 결속한다.

- canonical manifest와 detached signature envelope
- exact product expectation, immutable key policy와 signature provider
- source-neutral bundle byte source
- deterministic layout과 current slot metadata snapshot
- inactive target slot ID
- bounded storage provider
- caller-owned aligned scratch와 deadline

Installer는 compiler나 caller가 manifest authorization을 미리 수행했다는 표식을 신뢰하지 않는다.
`ribon_update_manifest_authorize()`를 다시 호출해 key usage, product, architecture, platform,
environment, protocol, hardware revision, rollback domain/sequence와 Ed25519 signature를 확인한다.
승인이 실패하면 erase, write와 metadata transition을 시작하지 않는다.

Bundle source는 exact range read만 제공한다. Network URI, UEFI file protocol, host path와 transport
retry는 source adapter 뒤에 있고 installer ABI에 들어오지 않는다. v1은 component 하나의 aligned
backing span이 caller scratch와 provider one-call transfer 상한에 들어갈 때만 설치한다.

## Transaction 순서

Installer는 다음 순서를 고정한다.

1. Request, provider, layout, current metadata와 bundle range를 검증한다.
2. Manifest와 signature envelope를 독립 승인한다.
3. Target이 active slot이 아니며 새 install 또는 same-identity resume가 가능한지 확인한다.
4. Manifest digest와 ordered component digest의 image-set identity를 계산한다.
5. `EMPTY`, `BAD` 또는 inactive `CONFIRMED`에서 `STAGING` successor를 만들거나 같은 identity의
   `STAGING`, `VERIFIED`, `PENDING` resume state를 승인한다.
6. 각 component를 manifest order로 exact read하고 content digest를 확인한다.
7. Aligned backing tail을 0으로 만든 뒤 semantic erase와 write를 수행한다.
8. 모든 component write 뒤 provider flush를 성공시킨다.
9. 각 backing range를 전부 다시 읽고 exact content digest와 zero tail을 확인한다.
10. 같은 identity로 `STAGING -> VERIFIED` successor와 receipt를 반환한다.

Metadata successor 반환은 media commit과 다르다. Product adapter는 verified successor를 redundant
metadata media에 기록하고 flush한 뒤 independent reader로 재개방해야 한다. Installer가 실패하면
`VERIFIED` successor를 반환하지 않는다. Partial bytes가 남을 수 있으나 current durable metadata가
`EMPTY` 또는 이전 state인 동안 boot authority가 되지 않는다.

Crash-consistent product는 loose successor를 직접 commit하지 않고
{doc}`transaction-journal-v1`의 coordinator를 사용한다. 이 경로는 durable journal을 유일한 current
metadata authority로 사용하며 `STAGING`, payload flush, `VERIFIED`, `PENDING`의 ordering과 idempotent
resume를 함께 닫는다.

## UEFI Block I/O adapter

`x86_64-uefi-update-recovery` product만 UEFI update adapter를 link한다. Adapter는 최대 16개의
Block I/O handle을 caller-owned array로 조사하며 다음 조건을 모두 만족하는 media가 정확히 하나일
때만 연다.

- media present, writable, non-partition handle
- exact 512-byte logical block와 bounded `IoAlign`
- product binding과 같은 capacity
- 64 KiB offset의 canonical 1024-byte anchor
- exact media identity와 layout identity digest
- anchor SHA-256, CRC32C와 zero reserved bytes
- independent layout reader가 승인한 512-byte identity

0개 또는 복수 match, handle capacity 초과, geometry/identity/integrity mismatch는 fail-closed다.
UEFI adapter는 product의 A/B 정책이나 manifest 의미를 판정하지 않고 Block I/O read, write,
logical zeroization과 flush만 generic provider로 투영한다. v1 `erase`는 discard나 physical erase가
아니라 exact zero block write다.

Metadata region의 앞 두 512-byte copy는 byte-identical해야 한다. Write path는 두 copy를 쓰고
firmware flush를 호출하며, product는 즉시 다시 읽어 canonical codec와 `VERIFIED` identity를
검증한다. 이것은 QEMU reference behavior이며 physical power-loss atomicity를 주장하지 않는다.

## Deterministic q35 media

Reference generator는 build root 아래에 다음 artifact를 만든다.

| Artifact | 의미 |
| --- | --- |
| `update-disk.raw` | 64 MiB pristine GPT-backed update media |
| `layout.bin` | canonical 512-byte layout identity |
| `update.man` | product-bound canonical manifest |
| `update.sig` | Ed25519 detached signature envelope |
| `update.bin` | ordered exact component bundle |
| `provenance.json` | 모든 input/output size와 SHA-256 |

Protective MBR, primary/backup GPT header와 table, deterministic GUID, media anchor, initial
`A=CONFIRMED/B=EMPTY` metadata와 active-slot bytes가 byte deterministic하다. GPT는 reference image와
independent inspector의 evidence format이며 generic installer나 UEFI adapter의 required parser가
아니다.

Harness는 pristine disk digest를 provenance와 먼저 비교한 뒤 results root에 runtime copy를 만든다.
첫 q35/OVMF boot는 bundle을 B에 설치하고 `VERIFIED`를 commit한다. 독립 inspector가 GPT mirror,
anchor/layout, redundant metadata, active-slot digest와 installed component digest를 확인한다. 두 번째
boot는 같은 runtime disk에서 `VERIFIED`를 재개방한다. 각 QEMU process group은 terminal marker 뒤
종료되고 forced kill이 하나라도 필요하면 gate가 실패한다. Network device는 연결하지 않는다.

## Product graph와 normal surface

Recovery product manifest는 provider class, layout/media identity와 read, writer, metadata, flush
service ID를 exact binding으로 선언한다. Composer는 service kind와 complete capability set을 다시
검사한다. Normal bootloader product는 `update_storage`, inactive writer service와 writer capability를
가질 수 없다. Normal `UEFI_SRCS`에는 `update_storage.c`, installer와 transaction coordinator가
포함되지 않는다.

## 검증과 비주장

Host installer test는 정상 설치, active-slot byte 불변, component corruption, short bundle read,
short provider write, 부족한 scratch, active-slot target과 signature corruption을 실행한다. Fixture
test는 두 독립 output root의 모든 생성 byte를 비교하고 short disk, GPT, active slot, installed
component와 metadata corruption을 independent inspector가 거부하는지 확인한다. Mock UEFI test는
handle filtering/capacity, duplicate match, media/anchor identity, stale-context revocation, redundant
metadata와 read/write/flush fault를 실행한다.

이 계약의 실행 claim은 “Ribon recovery product가 q35 UEFI Block I/O media의 inactive slot에
product-bound signed bundle을 설치하고 재부팅 뒤 `VERIFIED` 상태를 재개방했다”까지다. 다음은
claim이 아니다.

- 실제 RPi5 또는 물리 block/flash 장치 성공
- 전원 차단 중 atomic commit 또는 storage wear 내구성
- firmware가 보장하는 physical erase/discard
- network OTA, fleet rollout 또는 OS health confirmation
- production UEFI Secure Boot, measured boot 또는 key provisioning
- 새 slot의 boot, confirmation이나 rollback 완료
