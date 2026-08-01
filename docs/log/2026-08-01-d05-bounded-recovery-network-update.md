---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-08-01
code_paths:
  - include/Ribon/network/
  - src/common/net/
  - src/environments/uefi-app/recovery_network.c
  - targets/x86_64-uefi-network-update-recovery/entry.c
  - tools/qemu_recovery_network_update.py
tests:
  - make check-recovery-network-update
  - qstar --file qstar.lua test --suite //tests:recovery_network_tests
hardware:
  - qemu-q35-uefi
  - rpi5-not-run
supersedes:
  - none
---

# D05 recovery-only bounded network update 실행 기록

## 구현 결과

Recovery/provisioning product에만 선택되는 generic object fetch ABI, generated product binding,
allocation-free TFTP guard를 추가했다. UEFI adapter는 PXE Base Code를 우선 probe하고 absent일 때
exact-one SNP provider에서 ARP, IPv4, UDP, TFTP의 최소 outbound subset을 실행한다.

QEMU recovery target은 manifest, Ed25519 signature envelope, bundle을 각각 product-selected path에서
fixed RAM buffer로 가져온 뒤 D01 authorization과 D02/D04 transactional installer에 연결한다.
Network adapter가 writer나 slot transition을 직접 호출하지 않는다.

## First divergence와 해소

첫 actual QEMU boot에서 OVMF가 e1000에 `EFI_PXE_BASE_CODE_PROTOCOL`을 설치하지 않아
`pxe-capability-not-found`로 fail-closed했다. 허위 host transport로 대체하지 않고 같은 UEFI
environment adapter 안에 SNP fallback을 추가했다. 최종 runtime marker는
`RIBON-D05-UEFI-SNP-TFTP-CAPABILITY-OK`이며 evidence report의 transport는
`uefi-snp-bounded-tftp`다.

## 검증 evidence

- Host/unit: retry, timeout, capacity, output wipe, canonical path, OACK, duplicate, reorder,
  short final block, remote error, 10,000 seeded hostile packets
- Sanitizer: ASAN/UBSAN으로 같은 corpus 실행
- Cross compile: generic recovery/TFTP source를 x86_64, AArch64, RISC-V freestanding compile
- Product graph: normal mode, missing binding/capability, wrong service, traversal path, retry/size 상한 거부
- Normal binary: normal UEFI link map에 network transport/TFTP/recovery adapter와 update writer symbol 0
- QEMU runtime: q35 TCG, OVMF, e1000 SNP, restricted user-mode TFTP로 3 objects을 두 번
  fetch하고 inactive B를 generation 4 `PENDING`으로 install/reopen
- Cleanup: 두 boot 모두 process-group cleanup complete, forced kill 0, active A digest 불변

QEMU serial, disk inspector JSON, result JSON, firmware/application/product/TFTP object hash는
`build/d05/targets/x86_64-uefi-network-update-recovery/results/`에 남겨두었다. Build output은
source control evidence가 아니므로 commit하지 않는다.

## 비주장

출장 중이므로 RPi5 실기기를 실행하지 않았다. TLS, Internet OTA, DHCP/DNS,
production server, physical NIC, hardware anti-replay, physical power-loss durability, pending image의 실제
OS boot/confirmation은 이 라운드의 evidence가 아니다.
