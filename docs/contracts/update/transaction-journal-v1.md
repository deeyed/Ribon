---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-08-01
code_paths:
  - include/Ribon/update/transaction.h
  - include/Ribon/update/installer.h
  - src/update/transaction.c
  - src/update/installer.c
  - targets/x86_64-uefi-update-recovery/entry.c
  - tools/inspect_qemu_update_transaction.py
  - tools/qemu_update_power_cut.py
tests:
  - make check-update-power-cut
  - make check-qemu-update-power-cut
  - make check-update-installer
  - make check-update-storage-graphs
  - qstar --file qstar.lua check
hardware:
  - qemu-q35-uefi
supersedes:
  - unordered slot metadata installation
---

# Update transaction journal v1 계약

Update transaction journal v1은 승인된 inactive-slot 설치를 `STAGING`, payload, `VERIFIED`,
`PENDING`의 durable 순서로 닫는다. Generic coordinator는 architecture, firmware, OS, filesystem과
network transport를 알지 않는다. Product가 선택한 bounded storage provider, signed bundle source,
layout과 authorization input만 사용한다.

이 계약의 `PENDING`은 설치 완료 receipt다. 새 image의 boot authority는 아니며 protected rollback
state와 boot-attempt commit을 별도로 성공시켜야 한다.

## Authority와 journal range

`RibonUpdateTransactionJournal`은 canonical layout의 `update-journal` range, exact storage provider,
최소 허용 generation과 deadline을 묶는다. Range는 provider capacity와 같은 layout identity에
속하고 read/write alignment와 one-call transfer 상한을 만족해야 한다.

v1은 range 앞 3072 byte를 다음 순서로 사용한다.

| 상대 offset | 길이 | object |
| ---: | ---: | --- |
| 0 | 1024 | record slot 0 |
| 1024 | 1024 | record slot 1 |
| 2048 | 512 | selector slot 0 |
| 2560 | 512 | selector slot 1 |

나머지 range는 후속 ABI를 위한 product-owned reserve다. Record와 selector를 native C struct로
저장하지 않고 explicit little-endian writer/reader로 직렬화한다. 모든 size, offset, enum, flag,
reserved byte, generation과 digest 결속을 다시 검사한다.

## Record wire

Record는 exact 1024 byte다.

| Offset | 길이 | 내용 |
| ---: | ---: | --- |
| 0 | 32 | `RIBON-UPDATE-TXN-RECORD-V1` magic과 NUL padding |
| 32 | 2 | ABI version 1 |
| 34 | 2 | header byte 수 240 |
| 36 | 4 | total byte 수 1024 |
| 40 | 4 | complete phase 1 |
| 44 | 4 | target slot ID |
| 48 | 4 | target slot state |
| 52 | 4 | zero flags |
| 56 | 8 | positive journal generation |
| 64 | 8 | predecessor generation |
| 72 | 32 | predecessor record digest |
| 104 | 32 | embedded metadata SHA-256 |
| 136 | 32 | target manifest digest |
| 168 | 32 | target ordered image-set digest |
| 200 | 32 | target layout digest |
| 232 | 8 | zero reserved |
| 240 | 512 | canonical slot metadata wire |
| 752 | 32 | bytes 0..751의 SHA-256 |
| 784 | 4 | bytes 0..783의 CRC32C |
| 788 | 236 | zero reserved |

Generation 1은 predecessor generation과 digest가 모두 0이다. 이후 record는 정확히
`generation - 1`과 직전 selected record digest를 포함한다. Embedded metadata generation, target
state와 세 identity digest는 record header와 exact match해야 한다.

## Selector wire

Selector는 exact 512 byte다.

| Offset | 길이 | 내용 |
| ---: | ---: | --- |
| 0 | 32 | `RIBON-UPDATE-TXN-SELECT-V1` magic과 NUL padding |
| 32 | 2 | ABI version 1 |
| 34 | 2 | header byte 수 128 |
| 36 | 4 | total byte 수 512 |
| 40 | 4 | selected record slot |
| 44 | 4 | zero flags |
| 48 | 8 | selected generation |
| 56 | 32 | selected record digest |
| 88 | 8 | predecessor generation |
| 96 | 32 | predecessor record digest |
| 128 | 32 | bytes 0..127의 SHA-256 |
| 160 | 4 | bytes 0..159의 CRC32C |
| 164 | 348 | zero reserved |

Selector는 commit point다. Selector가 가리키지 않는 complete record는 durable authorization이
아니다.

## Record commit 순서

다음 generation은 selected selector가 가리키지 않는 record와 selector slot을 사용한다.

```text
encode complete record
  -> inactive record exact write
  -> provider flush
  -> full record readback와 byte-exact 비교
  -> inactive selector exact write
  -> provider flush
  -> journal reopen
  -> generation, target, state와 record digest 재검증
```

Record slot은 두 개뿐이므로 새 record를 쓰는 시점에 두 generation 전 record는 덮어쓸 수 있다.
따라서 open은 predecessor object 자체가 여전히 존재할 것을 요구하지 않는다. 대신 newest record와
selector가 직전 generation과 digest를 함께 결속하는지 검사한다.

## Scanner와 replay 의미

두 selector를 독립 decode한 뒤 다음 규칙을 적용한다.

- 모두 0이면 uninitialized다. 유효 selector 없이 non-zero byte가 있으면 corruption이다.
- 두 valid selector의 generation이 같으면 내용이 같더라도 conflict다.
- 서로 다른 generation이면 큰 값을 고른다. 낮은 stale selector는 authority를 낮추지 않는다.
- newest selector가 가리키는 record가 short, corrupt 또는 digest mismatch면 이전 selector로
  fallback하지 않는다.
- selected generation이 product/provider가 공급한 `minimum_generation`보다 작으면 replay다.

Software journal만 포함한 media 전체를 과거의 완전한 snapshot으로 되돌리면 내부 byte만으로
구분할 수 없다. Production anti-replay claim은 monotonic authenticated storage가 공급하는 external
minimum generation과 별도 hardware evidence를 요구한다.

## 설치 state와 resume

Coordinator는 매 진입 시 journal을 다시 열고 signed manifest authorization과 product/layout
identity를 다시 검사한다. Caller가 제공한 mutable metadata snapshot은 받지 않는다.

```text
durable predecessor
  -> STAGING record commit
  -> component exact source read
  -> inactive range erase/write
  -> component별 full backing readback
  -> payload flush
  -> VERIFIED record commit
  -> PENDING(attempts=1..32) record commit
```

`EMPTY`, `BAD` 또는 inactive `CONFIRMED`는 새 `STAGING` identity를 만든다. 같은 manifest,
image-set, image generation과 layout identity의 `STAGING`은 payload 설치부터, `VERIFIED`는
`PENDING` commit부터 재개한다. 동일 identity의 `PENDING` 재호출은 generation을 증가시키거나
두 번째 pending 의미를 만들지 않고 selected receipt를 반환한다. Identity가 다르면 fail-closed다.

Payload가 일부 기록되어도 durable state가 `STAGING`이면 실행 authority가 아니다. Component마다
exact content와 aligned zero tail을 모두 다시 읽어 확인하고 provider flush가 성공한 뒤에만
`VERIFIED`를 commit한다.

## Stable fault point

Observer ABI는 storage authority가 없는 pointer-free event를 operation 전후에 전달한다. Stable
operation ID는 journal record write/flush/readback, selector write/flush, reopen, bundle read,
slot erase/write/readback과 payload flush다. Callback failure는 before event에서 operation을 막고,
after event에서는 결과가 이미 발생했을 수 있는 ambiguous interruption으로 닫는다. Caller는
interruption 뒤 RAM state로 계속하지 않고 durable media를 reopen해야 한다.

Reference two-component fixture는 clean trace의 54개 before/after event 각각에서 fail-stop과 power-cut
replay를 실행한다. 모든 cut에서 active confirmed predecessor가 유지되고 대상이 confirmed가 되지
않으며, clean retry는 정확히 한 `PENDING` generation으로 수렴한다.

## Protected state와 boot 결합

Update journal의 `PENDING(N+1)` commit만으로 image를 transfer하면 안 된다. Boot selection은 최소한
다음을 모두 exact identity로 결속해야 한다.

1. Update journal이 target slot, manifest와 image-set을 `PENDING`으로 승인한다.
2. Protected-state journal이 confirmed floor `N`에서 `begin_trial(N+1)`을 durable commit한다.
3. 실제 transfer 전 `consume_trial_attempt()`가 남은 attempt 감소를 durable commit한다.

Update `PENDING` 뒤 protected-state commit 전에 crash가 나면 candidate는 inert pending으로 남고
재시도가 trial을 열 수 있다. Protected-state floor를 낮추거나 journal state만 보고 candidate를
부팅하는 fallback은 금지한다. Confirmation은 OS-specific health receipt와 두 journal identity를
확인한 후 별도 단계에서 수행한다.

## q35 reference evidence

Host crash model은 write가 volatile media에 반영되고 flush만 durable media로 복사된다고 모델링한다.
Power cut은 volatile byte를 durable snapshot으로 복원한다. Stable 54개 event 전수 주입 외에 short
write, flush failure, readback mutation, stale selector, same-generation selector conflict, external-floor
whole-media replay, torn newest record와 torn inactive selector를 검사한다.

QEMU gate는 host model이 만든 다음 세 crash snapshot을 q35/OVMF recovery product에서 연다.

- STAGING selector commit 직후
- payload flush 직후
- VERIFIED selector commit 직후

각 snapshot은 recovery boot와 같은 disk의 reopen boot를 수행한다. Independent inspector는 GPT mirror,
media anchor, layout, journal wire, active predecessor digest, `B=PENDING` generation 4와 installed component
digest를 확인한다. Network device는 없다. ESP의 `NvVars`는 OVMF가 생성할 수 있는 firmware-owned
mutable output이며 signed update input digest에서 제외하고 별도 hash로 보존한다.

## Claim과 non-claim

이 계약이 여는 claim은 다음까지다.

> Reference flush model의 모든 stable operation boundary와 선택된 q35 persistent crash snapshot에서
> Ribon update transaction은 active confirmed predecessor를 잃지 않고 idempotent `PENDING`으로
> 복구한다.

다음은 claim이 아니다.

- 실제 전원 차단, capacitor/cache/controller 내부 동작 또는 거짓 성공을 반환하는 flush
- physical flash atomicity, wear, bad-block 관리와 실제 RPi5 storage 성공
- hostile replay를 막는 production monotonic hardware provider
- network OTA, fleet rollout, OS health confirmation 또는 새 slot의 실제 boot 성공
- production UEFI Secure Boot, measured boot와 key provisioning
