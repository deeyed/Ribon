---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-26
code_paths:
  - src/common/net/
  - src/plugins/transports/
  - src/plugins/drivers/
  - src/plugins/update/
  - products/
tests:
  - ribon-network-parser-fuzz
  - ribon-recovery-download-test
  - ribon-network-timeout-test
  - ribon-normal-network-object-graph-lint
hardware:
  - none
supersedes:
  - platform-adapter-owned recovery network
---

# 복구 네트워크 Plugin 표면 계약

Networking은 transport, network driver, recovery policy plugin을 조합한 선택적 product
surface다. Normal boot product는 network link, address configuration, name resolution,
download 성공에 의존하지 않는다.

## Object graph

Network plugin은 recovery 또는 provisioning product가 명시적으로 선택할 때만 링크한다.
Normal product의 final link map에는 network driver, protocol parser, firmware network
binding, download client object가 없어야 한다.

## 진입 조건

Recovery network는 다음 trigger와 product policy가 함께 허용할 때만 열린다.

- bootable normal candidate 부재
- attempt budget 소진
- 서명된 recovery request
- physical-presence input
- manufacturing provisioning policy

Network failure는 검증되지 않은 candidate 실행, anti-rollback 완화, trust anchor 변경을
허용하지 않는다.

## Protocol 범위

첫 bounded client surface는 다음으로 제한한다.

- Ethernet 또는 firmware-provided transport
- 한 개 interface와 한 개 active transaction
- IPv4 static 또는 bounded DHCP
- 선택적 bounded DNS
- 단일 outbound component download
- response, redirect, header, component size 상한
- 전체 operation deadline과 retry budget

Inbound listener, interactive shell, generic socket API, server, multicast discovery,
unbounded fragmentation reassembly는 별도 product contract 없이 포함하지 않는다.

## Transport plugin

| Environment/platform | 허용 transport 예 |
| --- | --- |
| UEFI application | SNP, PXE Base Code, firmware HTTP |
| BIOS client | PXE/UNDI |
| RPi5 raw-FDT | 검증된 RP1/PCIe MAC/PHY driver |
| RISC-V UEFI | UEFI network service |
| RISC-V SBI | selected virtio 또는 board NIC driver |

Transport plugin은 native handle을 opaque context에 보관한다. Generic net code와 update
policy는 EFI, PXE, MMIO type을 직접 소비하지 않는다.

## Trust

Transport는 신뢰 경계 밖에 있다. TLS 성공은 manifest signature와 payload digest를
대체하지 않는다. HTTP와 test transport도 동일한 artifact 검증을 통과해야 inactive
destination에 `VERIFIED`를 기록할 수 있다.

TLS trust store, pinned key, secure-time 정책은 provisioning과 security plugin 계약이
소유한다. Secure time이 없다는 이유로 sequence와 signature 검증을 완화하지 않는다.

## Download와 storage

Payload는 bounded chunk로 inactive destination에 streaming한다. Chunk write 성공과
전체 image reread verification은 별도 단계다. Partial download는 `STAGING`에서
벗어나지 않는다.

Network plugin은 active destination 선택과 bootable state transition을 직접 수행하지
않는다.
