---
doc_type: canonical
status: accepted
authority: normative
last_verified: 2026-08-01
code_paths:
  - include/Ribon/boot/plan.h
  - include/Ribon/policy/ribos.h
  - include/Ribon/service/directory.h
  - src/common/boot.c
  - src/plugins/update/
  - src/plugins/policy/
  - src/plugins/watchdog/
  - src/update/transaction.c
  - src/protocols/
  - products/
tests:
  - make check-boot-lifecycle
  - ribon-update-power-loss-test
  - make check-update-power-cut
  - ribon-product-mode-graph-lint
hardware:
  - none
supersedes:
  - profile-owned boot lifecycle
---

# 부트·업데이트·복구 생명주기

Ribon Boot Library는 environment trust input과 다음 실행 단계 사이에서 image 선택,
검증, commit, transfer 순서를 강제한다. Update, recovery, overseer는 선택된 product의
policy와 service plugin이며 Core의 고정 OS 의미론이 아니다.

## 부트 lifecycle

```text
CAPTURE
  -> VALIDATE_PRODUCT
  -> NORMALIZE_ENVIRONMENT
  -> SELECT_SOURCE
  -> VERIFY_MANIFEST
  -> LOAD_IMAGE
  -> PREPARE_PROTOCOL
  -> COMMIT_ATTEMPT
  -> QUIESCE_ENVIRONMENT
  -> TRANSFER
```

`RibonBootTransaction`은 caller-owned object이며, `CAPTURE`에서만 초기화할 수 있다.
성공 stage는 되돌아가지 않는다. 오류는 `FAILED` terminal stage와
`RibonBootFailureReceipt`로 고정한다. Receipt는 stage, stable reason, static provider ID와
입력·출력 byte, component, retry 소비량만 포함하며 native pointer를 포함하지 않는다.

| Stage | 허용하는 외부 효과 | 실패 뒤 상태 |
| --- | --- | --- |
| `CAPTURE` | 없음 | `FAILED` |
| `VALIDATE_PRODUCT` | 없음 | `FAILED` |
| `NORMALIZE_ENVIRONMENT` | caller-owned map과 native input 정규화 | `FAILED` |
| `SELECT_SOURCE` | 없음 | `FAILED` |
| `VERIFY_MANIFEST` | 없음 | `FAILED` |
| `LOAD_IMAGE` | 선택 source의 bounded read | `FAILED` |
| `PREPARE_PROTOCOL` | caller-owned handoff buffer write | `FAILED` |
| `COMMIT_ATTEMPT` | metadata write와 flush | `FAILED` |
| `QUIESCE_ENVIRONMENT` | 선택된 environment closure | `FAILED` |
| `TRANSFER` | terminal architecture entry | 반환하지 않음 |

### Capture

Architecture와 environment consumer는 native entry state를 typed descriptor로 변환한다.
Native pointer는 address, size, alignment, lifetime, reclaim 조건과 함께 전달한다.

### Validate product

Generated product descriptor, plugin ABI, capability graph, phase dependency, resource budget을
검증한다. 검증 실패 전에는 plugin callback을 호출하지 않는다.

### Normalize environment

Firmware table, FDT, memory map, boot source, reset reason을 검증하고 immutable snapshot으로
동결한다. Machine-specific resource가 필요하면 선택된 typed port service를 통해
검증하며 monolithic platform fact table은 만들지 않는다. 이후 native source를 다시
해석하지 않는다.

### Select and verify

Boot policy는 configuration candidate가 명시한 source, protocol과 image-format tuple을 선택한다.
Unknown required configuration key, non-canonical path, unsupported tuple과 same-priority candidate는
selection failure다. `VERIFY_MANIFEST`는 stable source name과 선택된 candidate 범위를 검증한다.
이 단계는 signature, digest, anti-rollback 또는 network metadata를 검증하지 않는다. 그런 trust
assertion은 Security와 Update Policy plugin 계약이 결정하며, 검증 결과는 이후 transaction input으로
전달된다.

`LOAD_IMAGE`는 selected boot-source authority만 호출한다. 한 callback의 deadline과 product
retry budget을 동시에 적용하고, 자동 fallback source·protocol·network discovery를 수행하지
않는다. Source가 memory mapping을 zero-copy로 노출하려면 environment service가 input/output
alias를 명시적으로 보장해야 한다.

Read-only media parser는 GPT/protective MBR, filesystem과 path를 각각 검증한다. Parser failure는
memory, other filesystem 또는 network source fallback을 열지 않는다. Normal product의 reader는
inactive-slot writer와 mutable filesystem authority를 포함하지 않는다.

### Ribos policy adapter

Ribos가 selected policy이면 generated binding이 schema, helper route, mode/phase,
timer/watchdog와 resource limit을 고정한다. Generic adapter는 Ribon Core arena와 typed
service만 알고 VM target core는 Ribon symbol을 import하지 않는다.

```text
validate generated binding
  -> product artifact authorization
  -> independent verifier + PreparedProgram
  -> arm required watchdog
  -> bounded VM execution
  -> validate sealed BootAction
  -> consume action once
  -> COMMIT_ATTEMPT
  -> QUIESCE_ENVIRONMENT
```

Adapter는 `TRANSFER`를 호출하지 않는다. PolicyError, VmFault, action rejection과
post-consume commit/quiesce failure는 external `.rba`가 없어도 실행되는 compiled
factory recovery에 최대 한 번 전달한다. Failure 뒤 policy 재실행이나 consumed
action 재사용은 허용하지 않는다.

### Prepare protocol

Boot Protocol은 검증된 component plan으로 handoff와 entry contract를 생성한다. Protocol
실패를 다른 protocol의 입력으로 재해석하지 않는다.

### Commit

`COMMIT_ATTEMPT`는 attempt record를 persistent-metadata authority에 기록하고
storage-flush authority의 durability barrier가 성공한 뒤에만 완료된다. Partial write,
write failure, flush failure, deadline expiry는 모두 terminal receipt를 남기고 transfer를
금지한다. Commit 뒤 source 선택을 바꾸려면 명시적인 failure transition을 수행한다.

### Quiesce

Environment는 final memory map과 reclaim 상태를 확정하고 selected
`environment-quiesce` authority를 호출한다. UEFI처럼 final map을 얻는 과정이 있는
environment는 commit 뒤 source 선택 없이 handoff만 갱신하고, native closure가 성공한 뒤
transaction quiesce를 수행한다. Quiesce 뒤 종료된 firmware service를 호출하지 않는다.
Closure callback은 bounded non-blocking operation이어야 한다. Closure는 timer authority를
동시에 철회할 수 있으므로 Boot Library는 closure callback 자체를 계측하려고 timer를
재호출하지 않는다. Source read, protocol handoff, metadata write/flush의 deadline은 closure
전에 monotonic timer로 검사한다.

### Transfer

Architecture backend는 cache와 privilege state를 정규화하고 protocol entry ABI를
적용한다. Transfer는 반환하지 않는다.

## Mode product

| Mode | Network | Mutable storage | 목적 |
| --- | --- | --- | --- |
| normal | 금지 | attempt journal만 허용 | 검증된 candidate를 bounded time에 실행 |
| recovery | 명시 선택 | inactive/recovery destination | known-good 복구 |
| provisioning | product와 physical-presence 정책 | trust와 초기 metadata | 제조와 등록 |
| diagnostic | 별도 graph | evidence contract만 허용 | 개발 및 검증 |

Mode는 runtime flag만이 아니라 product object graph다. Normal product는 recovery
transport, inactive writer, diagnostic fixture를 링크하지 않는다.
Normal Ribos binding도 network, flash와 update-writer helper route를 생성하지 않는다.
Recovery/provisioning policy는 같은 callback을 flag로 여는 것이 아니라 별도 signed
product graph로 구성한다.

## Update

Update policy plugin은 active confirmed destination을 덮어쓰지 않는다. Payload는 inactive
destination에 bounded chunk로 기록하며 flush, reread, digest, manifest, compatibility,
anti-rollback 검증 뒤에만 `VERIFIED`가 된다.

```text
EMPTY -> STAGING -> VERIFIED -> PENDING -> CONFIRMED
          |           |           |
          +-----------+-----------+-> BAD
```

Power loss가 어느 write 경계에서 발생해도 이전 committed generation과 active confirmed
image를 식별할 수 있어야 한다.

Storage contract는 equal A/B slots, immutable recovery, metadata/journal과 guard range를 canonical
layout digest에 결속한다. Metadata generation과 image generation은 독립적으로 증가하며 active
confirmed slot은 writer handle 대상이 아니다. Inactive slot은 semantic `STAGING` handle로만
read/write/erase할 수 있고 metadata generation이 바뀌면 handle은 stale이다. Wire와 provider 세부
계약은 {doc}`../../contracts/storage/bounded-update-slot-provider-v1`이 소유한다.

Crash-consistent install은 {doc}`../../contracts/update/transaction-journal-v1`의 selector-committed
journal을 사용한다. Canonical durable 순서는 `STAGING -> component write/full readback -> payload
flush -> VERIFIED -> PENDING`이다. Interruption 뒤 RAM successor를 이어 쓰지 않고 journal을
reopen하며, same-identity `PENDING` retry는 새 generation을 만들지 않는다.

`PENDING`은 transfer authority가 아니다. Protected state에서 exact successor trial과 attempt 소비를
commit한 뒤에만 candidate transfer가 가능하다. 두 journal 사이 crash는 pending candidate를 inert
상태로 남기며 active confirmed predecessor를 계속 보존한다.

## Confirmation

Generic confirmation engine은 product와 Boot Protocol ID/version, slot, image generation,
manifest digest/sequence, policy version, fresh boot nonce와 단조 attempt sequence를 하나의
canonical attempt binding으로 묶는다. Pending payload로 제어를 넘기기 전에 protected journal에
binding을 commit하고 attempt를 선차감한다.

OS가 생성한 health payload의 의미는 선택된 Boot Protocol만 판정한다. Generic engine은 payload를
해석하지 않고 bounded byte view와 SHA-256을 protocol callback에 넘긴다. Callback 성공, dedicated
confirmation key authorization, Ed25519 검증, exact binding과 두 journal identity가 모두 일치해야
protected floor와 update active slot을 승격한다. 동일 envelope 재전달은 generation을 늘리지 않는
성공이며, 이전 attempt·nonce, 다른 slot/image/manifest/product/protocol은 fail-closed한다. Wire와
전이 계약은 {doc}`../../contracts/update/boot-confirmation-v1`이 소유한다.

## Recovery

Recovery product는 bootable normal candidate 부재, attempt budget 소진, signed request,
physical presence 같은 명시 trigger로 진입한다. Network 성공만으로 candidate를
bootable로 만들지 않는다.

## Overseer

Generic Core는 resident overseer가 아니다. Reboot-time supervision은 watchdog, reset
reason, confirmation, attempt journal plugin을 조합한 policy product다. OS별 health와
fleet 의미론은 companion policy plugin이 소유한다.

EL2, M-mode, SMM, VMX root에 상주하는 monitor는 별도 product, ABI, threat model을
요구한다.
