---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-26
code_paths:
  - src/network/
  - src/recovery/
  - src/platform/
tests:
  - ribon-network-parser-fuzz
  - ribon-network-timeout-test
hardware:
  - none
supersedes:
  - none
---

# ADR: Networking은 bounded recovery client로 제한한다

## 맥락

OTA 복구에는 network가 필요하지만 normal boot가 DHCP, DNS, server availability에
의존하면 부팅 시간과 실패 원인이 외부 환경에 종속된다. 범용 socket과 server는
공격 표면과 memory 요구량을 크게 만든다.

## 결정

Network는 recovery와 provisioning object graph의 outbound client다. Ethernet, 한 개
interface, bounded transaction, 명시적 deadline을 기본 surface로 둔다. Normal boot는
network service를 초기화하지 않는다.

Transport는 신뢰하지 않으며 signed manifest와 payload digest를 항상 검증한다.

## 기각한 대안

### Normal boot마다 update 확인

Network 실패가 deterministic boot와 watchdog deadline을 깨뜨리므로 선택하지 않는다.

### Interactive network shell

인증, session, command authority와 parser surface가 recovery 목적을 넘어가므로 선택하지
않는다.

### TLS만으로 payload 신뢰

Firmware TLS, secure time, CA store 성공이 boot bundle authenticity를 대신하게 되어
선택하지 않는다.

## 결과

- UEFI는 firmware network protocol을 먼저 사용한다.
- BIOS는 PXE/UNDI를 compatibility adapter로 사용할 수 있다.
- Native NIC driver는 platform별 acceptance를 요구한다.
- Partial download는 bootable 상태가 아니다.
