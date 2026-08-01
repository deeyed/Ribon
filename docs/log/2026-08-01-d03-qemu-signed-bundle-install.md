---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-08-01
code_paths:
  - include/Ribon/update/installer.h
  - src/update/installer.c
  - src/environments/uefi-app/update_storage.c
  - targets/x86_64-uefi-update-recovery/entry.c
  - tools/make_qemu_update_fixture.py
  - tools/inspect_qemu_update_disk.py
  - tools/qemu_update_install.py
tests:
  - make check-update-installer
  - make check-uefi-update-storage
  - make check-qemu-update-install
hardware:
  - qemu-q35-uefi
  - rpi5-not-run
supersedes:
  - none
---

# D03 q35 UEFI signed bundle install 실행 기록

## 구현 결과

- Architecture/firmware-neutral installer가 manifest를 다시 승인한 뒤 inactive slot에 component를
  exact write하고 full readback digest와 zero tail을 확인한다.
- UEFI recovery adapter가 exact media anchor와 product layout/media identity로 writable Block I/O
  handle 하나만 선택한다.
- q35 validation product만 writer, metadata, flush service와 installer를 link한다. Normal UEFI source
  graph는 update writer를 포함하지 않는다.
- Generator가 deterministic 64 MiB GPT disk, A/B layout, signed manifest/envelope, bundle과 provenance를
  만든다.
- Harness는 pristine fixture를 results root에 복사하고 2회 QEMU boot, independent disk inspection과
  process-group cleanup receipt를 보존한다.

## 실행 evidence

QEMU 11.0.2 q35 TCG와 OVMF에서 첫 boot는
`RIBON-D03-UPDATE-INSTALLED-VERIFIED`, 두 번째 boot는
`RIBON-D03-UPDATE-REOPEN-VERIFIED`를 정확히 한 번 출력했다. 두 process 모두 forced kill 없이
cleanup됐고 network device는 비활성화됐다.

Independent inspector가 다음을 확인했다.

- active slot SHA-256은 설치 전후 동일
- slot B의 2개 4096-byte component digest가 manifest와 동일
- metadata generation 3, `A=CONFIRMED`, `B=VERIFIED`
- primary/backup GPT header와 table CRC 및 exact mirror
- media anchor, layout identity, duplicate metadata SHA-256/CRC32C

Result JSON은 pristine/runtime disk, ESP tree, bundle, manifest, signature envelope, product manifest,
OVMF, BOOTX64.EFI, serial log와 provenance hash를 기록한다. Fixture hostile test는 생성 byte
determinism과 short/GPT/active/component/metadata corruption rejection을 실행했다. Host installer test는
정상 install과 6개 fail-closed class를 실행했다.

## 비주장

출장으로 RPi5 실기기를 실행하지 않았다. 이 결과는 physical disk/flash write, power-loss atomicity,
wear behavior, network OTA, OS health confirmation, production UEFI Secure Boot 또는 새 slot boot
성공을 입증하지 않는다. UEFI erase callback은 logical zero write이며 physical erase가 아니다.
