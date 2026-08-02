---
doc_type: devlog
status: historical
authority: non-normative
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
hardware:
  - none
supersedes:
  - none
---

# R03 FreeBSD amd64 UEFI runtime

## 구현

- FreeBSD 15.1-RELEASE amd64 mini-memstick를 exact size/hash로 고정한 external descriptor와 validator를
  추가했다.
- 공식 raw image를 수정하지 않고 독립 product image의 FAT32 ESP에 Ribon, exact official loader와
  boot config를 조합하는 mountless deterministic composer를 추가했다.
- FreeBSD protocol을 generic firmware-managed terminal image transaction에 연결하고 `-h -s` load
  option을 bounded UTF-8 request로 전달했다.
- Firmware-managed PE validation은 file padding을 허용하되 direct PE loader의 엄격한 section copy
  조건은 유지했다.
- FreeBSD product를 object-graph, package hostility, QEMU evidence와 4-product hermetic build gate에
  편입했다.

## 실행 증거

QEMU q35 + OVMF에서 Ribon managed-launch marker 뒤 official loader revision 3.0, FreeBSD
15.1-RELEASE GENERIC amd64 kernel banner와 `Enter full pathname of shell or RETURN for /bin/sh:`를
관측했다. Harness result는 required-evidence-observed, process-group cleanup complete, forced kill
false를 기록한다.

이 실행은 evidence 관측 뒤 host harness가 QEMU를 종료한 것이므로 guest clean poweroff 증거가 아니다.
또한 checksum authority의 PGP signature presence는 기록했지만 signature verification은 수행하지
않았다. Physical hardware, network, installer, multi-user와 production secure boot는 검증하지 않았다.
