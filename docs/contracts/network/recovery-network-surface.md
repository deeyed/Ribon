---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-26
code_paths:
  - src/network/
  - src/recovery/
  - src/platform/
tests:
  - ribon-network-parser-fuzz
  - ribon-recovery-download-test
  - ribon-network-timeout-test
hardware:
  - none
supersedes:
  - none
---

# 복구 네트워크 표면 계약

Ribon networking은 recovery와 provisioning mode의 outbound client 기능이다. Normal boot는
네트워크 link, DHCP, DNS, download 성공에 의존하지 않는다.

## 진입 조건

Recovery network는 다음 trigger 중 하나와 build policy가 함께 허용할 때만 연다.

- bootable normal slot 부재
- attempt budget 소진
- 서명된 Parus recovery request
- physical-presence input
- manufacturing provisioning profile

Network failure는 검증되지 않은 slot 실행이나 rollback 완화를 허용하지 않는다.

## Protocol 범위

첫 production surface는 Ethernet client, IPv4, static 또는 DHCP, 단일 outbound
download transaction으로 제한한다.

- inbound listener 없음
- packet fragmentation 재조립 없음
- 한 개 interface와 한 개 active transaction
- bounded ARP, DHCP, DNS cache
- bounded header와 redirect 수
- response 및 component size 상한
- 전체 operation deadline
- retry마다 watchdog contract 준수

Wi-Fi, interactive shell, generic socket API, server, multicast discovery는 별도 ADR 없이
normal 또는 recovery image에 포함하지 않는다.

## Platform adapter

| Platform | Transport adapter |
| --- | --- |
| UEFI | SNP, PXE Base Code 또는 HTTP protocol |
| BIOS | PXE/UNDI 또는 명시적인 NIC adapter |
| RPi5 native | RP1/PCIe 및 MAC/PHY가 검증된 뒤의 native adapter |
| RISC-V UEFI | UEFI network protocol |
| RISC-V OpenSBI | board/virtio network adapter |

Firmware network service가 제공되어도 Core는 UEFI/PXE native type을 직접 소비하지 않는다.

## Trust

Transport는 신뢰 경계 밖에 있다. HTTPS를 사용해도 manifest signature와 payload digest를
검증한다. HTTP 또는 test transport도 signed artifact 검증을 통과해야만 inactive slot에
`VERIFIED`를 기록할 수 있다.

TLS trust store, pinned key, secure time 정책은 platform provisioning 계약으로 고정한다.
Secure time을 얻지 못하면 expiration 기반 정책을 완화하지 않고 sequence와 signature
기반 정책을 적용한다.

## Download와 storage

Payload는 전체를 RAM에 보관하지 않고 inactive slot에 bounded chunk로 streaming한다.
각 chunk write와 전체 image reread verification을 분리한다. Partial download는
`STAGING`에서 벗어나지 않는다.
