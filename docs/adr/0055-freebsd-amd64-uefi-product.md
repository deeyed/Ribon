---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-08-02
code_paths:
  - external/inputs/freebsd-amd64-15.1-release.json
  - products/bootmgr/manifests/x86_64-uefi-freebsd.json
  - src/protocols/os/freebsd/protocol.c
  - src/image-formats/pe_coff.c
  - tools/prepare_external_freebsd.py
  - tools/compose_freebsd_uefi.py
  - tools/qemu_target_smoke.py
tests:
  - make check-freebsd-package
  - make check-freebsd-uefi
  - make check-uefi-product-hermeticity
  - make check-object-graphs
hardware:
  - none
supersedes:
  - FreeBSD provider-only support state
---

# ADR 0055: FreeBSD amd64 UEFI product

## 결정

FreeBSD amd64는 별도 normal-mode product인 `bootmgr.x86_64-uefi-freebsd`로 지원한다.
Ribon Core, UEFI frontend와 terminal launcher는 FreeBSD stable ID 또는 디스크 layout을 알지 않는다.
`protocol.freebsd`만 `FIRMWARE_MANAGED_IMAGE`와 bounded load option을 선언하며, product graph가
PE/COFF validator와 generic UEFI launcher를 결합한다.

외부 authority는 FreeBSD 15.1-RELEASE amd64 mini-memstick image의 공식 URL, compressed/raw exact
size와 SHA-256, 공식 PGP-signed checksum 문서 identity를 source descriptor로 고정한다. 현재 build는
checksum 문서의 PGP 서명을 검증하지 않으므로 provenance에 `signature_verified=false`를 명시한다.
Hash 일치는 production authenticity 또는 Secure Boot를 뜻하지 않는다.

## Mountless deterministic composition

공식 raw image는 immutable cache다. Composer는 별도 product output으로 exact copy한 뒤 첫 EFI FAT32
partition만 bounded parser/writer로 변경한다.

1. 공식 `/EFI/BOOT/BOOTX64.EFI` loader bytes를 읽고 FreeBSD marker와 PE32+ amd64 EFI application
   identity를 검증한다.
2. 같은 exact bytes를 `/EFI/FREEBSD/LOADER.EFI`에 게시한다.
3. `/EFI/BOOT/BOOTX64.EFI`를 product-owned Ribon application으로 교체한다.
4. `/RIBON/BOOT.CFG`를 고정된 short-name 경로에 게시한다.
5. 공식 input의 before/after digest, loader digest와 composed disk digest를 result에 보존한다.

Host mount, privileged loop device, host filesystem timestamp 또는 firmware NVRAM state에 의존하지
않는다. FAT allocation은 first-fit, fixed timestamp와 양쪽 FAT 갱신으로 결정적이다. Fixture, external
Parus, Linux EFI와 FreeBSD product는 registry, objects, map, media와 results를 공유하지 않는다.

## PE/COFF validation 경계

FreeBSD loader는 일부 section에서 file alignment padding 때문에 `SizeOfRawData`가
`VirtualSize`보다 크고, section RVA가 `SectionAlignment`에 정렬되지 않는다. Firmware-managed
validation은 virtual span을 실행 memory authority로 사용하고 raw padding을 허용한다. 반면 Ribon
direct loader는 raw-to-virtual copy와 target address를 직접 소유하므로 raw span 초과와 unaligned RVA를
계속 거부한다. 따라서 firmware가 처리할 합법적 image 수용 때문에 direct loader 안전 계약을
완화하지 않는다.

## QEMU acceptance와 claim

QEMU q35 + OVMF evidence는 ordered/unique Ribon marker 뒤 공식 FreeBSD loader banner,
`FreeBSD 15.1-RELEASE` amd64 kernel banner와 single-user shell pathname prompt를 요구한다. Required
evidence가 관측되면 harness가 process group을 종료하며 cleanup complete와 forced kill false를
검사한다.

허용 claim은 “Ribon x86_64 UEFI product가 pinned FreeBSD 15.1 amd64 loader를 실행해 kernel과
single-user terminal prompt까지 도달한다”이다. Clean guest poweroff, multi-user login, installer,
network, physical hardware, production authenticity, Secure Boot와 모든 FreeBSD release 지원은 열지
않는다.
