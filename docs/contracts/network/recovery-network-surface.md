---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-08-01
code_paths:
  - include/Ribon/network/recovery.h
  - include/Ribon/network/tftp.h
  - src/common/net/recovery.c
  - src/common/net/tftp.c
  - src/environments/uefi-app/recovery_network.c
  - products/validation/manifests/x86_64-uefi-network-update-recovery.json
tests:
  - make check-recovery-network-update
  - make check-normal-media-surface
  - qstar --file qstar.lua test --suite //tests:recovery_network_tests
hardware:
  - qemu-q35-uefi
  - rpi5-not-run
supersedes:
  - platform-adapter-owned recovery network
---

# Recovery-only bounded network update 계약

Ribon network surface는 normal boot의 항상 활성화된 서비스가 아니다. Product graph가
`recovery` 또는 `provisioning` mode에서 exact `network-transport` authority를 선택한 경우에만
signed update object를 가져오는 outbound client다. Generic core는 object fetch 계약만 소유하고,
UEFI protocol과 packet I/O는 environment adapter가 소유한다.

## Product object graph

v1 product binding은 다음을 build time에 불변값으로 고정한다.

- exact service ID와 transport class
- 하나의 server IPv4, station IPv4, subnet mask
- 512..1468 byte TFTP block size
- 최대 3 retries와 모든 retry를 합한 30 s 이하 deadline
- manifest, signature envelope, bundle의 exact relative path와 각 object byte 상한

Runtime request에 URL, hostname, redirect, arbitrary path, interface 선택을 넣을 수 없다.
Object의 최대 크기는 16-bit TFTP block number가 wrap되기 전의 값보다 작아야 한다.

```text
generated product binding
        |
        v
generic bounded retry gate
        |
        v
recovery-only UEFI transport service
        |
        +-- exact-one usable IPv4 PXE Base Code MTFTP, if present
        `-- exact-one SNP + minimal ARP/IPv4/UDP/TFTP, if usable IPv4 PXE is absent
```

PXE provider가 여러 개이거나 malformed인 경우 SNP로 우회하지 않는다. PXE protocol이
IPv4 MTFTP 계약을 만족하는 PXE provider가 없거나 PXE child cardinality만으로 authority를
정할 수 없을 때 SNP fallback을 탐색한다. SNP는 exact-one physical transport authority여야
한다. 한 NIC의 IPv4와 IPv6 또는 중복 PXE child handle이 함께 보이더라도 exact-one SNP가
그 NIC를 고정하면 bounded SNP backend를 선택할 수 있다. 동일 MAC identity의 중복 SNP
child handle은 한 physical authority로 축약하지만 서로 다른 MAC이 하나라도 보이면 복수
NIC로 거부한다. PXE와 SNP 모두 모호하거나 malformed PXE/SNP surface가 보이면
fail-closed한다. Handle enumeration은 고정 용량이며 상한 초과도 모호성으로 거부한다.

## Generic fetch ABI

`RibonRecoveryNetworkProductBinding`은 generated product graph의 immutable input이다.
`RibonRecoveryNetworkRequest`는 provider 한 시도의 path, peer, output buffer, byte/block/deadline
bound를 담는다. `RibonRecoveryNetworkResult`는 exact received byte와 시도 수만 반환한다.

Generic gate는 다음을 보장한다.

- service kind, ID, typed operation ABI의 exact match
- timeout/I/O에만 bounded retry
- declared maximum을 넘는 provider receipt 거부
- 실패 뒤 caller staging buffer zeroization
- 전체 deadline을 시도 수로 나눈 per-attempt budget

Fetch 성공은 authenticity receipt가 아니다. 가져온 byte는 여전히 untrusted staging data다.

## Minimal SNP/TFTP state machine

SNP fallback은 heap, DHCP, DNS, IP fragmentation, inbound listener를 사용하지 않는다.

1. stopped SNP를 start하고 started SNP를 fixed-buffer initialize한다.
2. unicast/broadcast receive filter와 Ethernet header/MAC/MTU 계약을 검사한다.
3. product-selected static peer를 ARP request 하나로 resolve한다.
4. fixed client UDP port에서 product path의 `octet` RRQ와 `blksize`를 보낸다.
5. 첫 valid reply의 server transfer port를 lock한다.
6. IPv4 checksum, fragmentation, peer address, UDP port/length, TFTP opcode/block/option을 검사한다.
7. new DATA만 exact offset에 복사하고 duplicate DATA는 다시 ACK하며 reorder는 거부한다.
8. negotiated block보다 짧은 final DATA를 받은 뒤 exact byte receipt를 생성한다.

Receive poll, transmit recycle, packet count, output offset는 모두 fixed bound를 가진다. UDP checksum 0을
허용하는 IPv4 표면이며 transport confidentiality/authenticity를 주장하지 않는다.

## Update authority와 fail-closed order

Network adapter는 storage writer, slot state, trust anchor를 소유하지 않는다. Recovery product의
절차는 다음 순서를 바꾸지 않는다.

```text
fetch manifest/signature/bundle into bounded RAM
        -> D01 signature + product + rollback + component digest authorization
        -> D02 inactive-slot exact writer/readback
        -> D04 STAGING -> VERIFIED -> PENDING transaction
```

Unsigned, stale, product-mismatched, digest-mismatched, short, oversized input은 `PENDING` transition에
도달할 수 없다. Active/confirmed slot은 network adapter의 output이 아니다.

## Normal boot negative contract

Normal bootloader manifest는 `NETWORK_TRANSPORT`, inactive-slot writer/erase capability와 관련 service를
선택할 수 없다. Normal UEFI link map에는 recovery fetch, TFTP guard, UEFI recovery
network adapter, bounded-TFTP service symbol이 없어야 한다. 이 조건은
`check-normal-media-surface`가 binary symbol 단계에서 검사한다.

## 비주장

v1 evidence는 QEMU q35, OVMF, e1000 SNP, restricted user-mode TFTP에 한정된다. TLS,
Internet OTA, DHCP/DNS, production server operation, physical RPi5 Ethernet/Wi-Fi, hardware root key,
physical power-loss durability를 증명하지 않는다.
