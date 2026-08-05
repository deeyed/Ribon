---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-08-01
code_paths:
  - include/Ribon/network/recovery.h
  - src/common/net/
  - src/environments/uefi-app/recovery_network.c
  - products/validation/manifests/x86_64-uefi-network-update-recovery.json
tests:
  - make check-recovery-network-update
  - make check-normal-media-surface
hardware:
  - qemu-q35-uefi
  - rpi5-not-run
supersedes:
  - 0007-bounded-recovery-network
---

# ADR: Network update는 recovery-only bounded object transport다

## 맥락

Ribon은 update manifest, product-bound signature policy, inactive-slot writer, crash-consistent transaction을
이미 분리했다. 남은 network input을 normal boot에 포함하면 외부 장애가 deterministic
boot에 유입되고 일반 socket/API가 core attack surface를 늘린다. 반대로 host fake만으로는
firmware NIC, packet parser, TFTP, transactional install의 실제 경계를 증명할 수 없다.

## 결정

Network은 recovery/provisioning product가 exact service로 선택하는 outbound object transport로만
제공한다. Generic 계층은 endpoint/path/size/retry/deadline을 고정한 fetch ABI와 TFTP packet
guard를 소유한다. UEFI 계층은 exact-one usable IPv4 PXE Base Code를 우선하고, 해당
provider가 없거나 PXE child cardinality가 모호할 때 exact-one SNP의 minimal
ARP/IPv4/UDP/TFTP adapter로 fallback한다. EDK2가 한 NIC에 여러 PXE child handle을
게시하더라도 exact-one SNP가 physical transport authority를 고정할 수 있다. SNP도
서로 다른 MAC identity를 노출하거나 PXE/SNP surface 자체가 malformed이면
fail-closed한다. 동일 MAC의 중복 SNP child는 같은 physical authority로 축약한다.

Downloaded bytes에는 신뢰를 부여하지 않는다. Manifest signature, product identity, rollback
sequence, component digest가 모두 성공한 뒤에만 inactive-slot writer와 `PENDING` transaction을
실행한다. Normal product graph과 final link map은 network/update-writer reachability 0을
negative gate로 가진다.

## 검토한 대안

### Normal boot에서 항상 update 확인

Network availability와 timeout이 작은 deterministic loader의 성공 조건이 되므로 기각했다.

### 범용 socket, URL, HTTP/TLS runtime

DNS, redirect, certificate time, allocator, background I/O를 v1에 끌어들이고 policy authority를
널히므로 기각했다. 후속 transport는 같은 bounded object ABI를 따로 구현해야 한다.

### PXE Base Code만 지원

현재 QEMU 11.0.2 OVMF/e1000은 `EFI_PXE_BASE_CODE_PROTOCOL`을 제공하지 않았다. Host
fake로 우회하지 않고 SNP fallback을 구현해 actual QEMU fetch를 닫았다.

## 결과

- normal boot의 binary attack surface는 network 기능 추가 전과 동일하다.
- recovery product는 heap 없는 bounded packet state machine과 signed update transaction을 조합한다.
- transport provider는 OS, slot policy, key policy를 알지 못하므로 generic 경계를 유지한다.
- QEMU SNP evidence는 Internet/TLS/physical NIC/production OTA 주장으로 확장되지 않는다.
