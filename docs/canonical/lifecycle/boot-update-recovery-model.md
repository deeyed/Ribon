---
doc_type: canonical
status: accepted
authority: normative
last_verified: 2026-07-26
code_paths:
  - src/common/boot/
  - src/plugins/update/
  - src/plugins/policy/
  - src/plugins/watchdog/
  - src/protocols/
  - products/
tests:
  - ribon-boot-lifecycle-state-test
  - ribon-update-power-loss-test
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
  -> FREEZE_PLATFORM_FACTS
  -> SELECT_SOURCE
  -> VERIFY_MANIFEST
  -> LOAD_IMAGE
  -> PREPARE_PROTOCOL
  -> COMMIT_ATTEMPT
  -> QUIESCE_ENVIRONMENT
  -> TRANSFER
```

### Capture

Architecture와 environment consumer는 native entry state를 typed descriptor로 변환한다.
Native pointer는 address, size, alignment, lifetime, reclaim 조건과 함께 전달한다.

### Validate product

Generated product descriptor, plugin ABI, capability graph, phase dependency, resource budget을
검증한다. 검증 실패 전에는 plugin callback을 호출하지 않는다.

### Freeze platform facts

Firmware table, FDT, memory map, boot source, reset reason을 검증하고 immutable snapshot으로
동결한다. 이후 native source를 다시 해석하지 않는다.

### Select and verify

Boot policy가 source와 candidate를 선택한다. Image와 manifest는 선택된 security,
image-format, boot-protocol plugin의 모든 요구사항을 만족해야 한다.

### Prepare protocol

Boot Protocol은 검증된 component plan으로 handoff와 entry contract를 생성한다. Protocol
실패를 다른 protocol의 입력으로 재해석하지 않는다.

### Commit

Attempt journal과 필요한 watchdog state를 durable하게 기록한다. Commit 뒤 source 선택을
바꾸려면 명시적인 failure transition을 수행한다.

### Quiesce

Environment는 final memory map과 reclaim 상태를 확정하고 종료할 service를 폐쇄한다.
Quiesce 뒤 종료된 firmware service를 호출하지 않는다.

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

## Update

Update policy plugin은 active confirmed destination을 덮어쓰지 않는다. Payload는 inactive
destination에 bounded chunk로 기록하며 flush, reread, digest, manifest, compatibility,
anti-rollback 검증 뒤에만 `VERIFIED`가 된다.

```text
EMPTY -> STAGING -> VERIFIED -> PENDING -> CONFIRMED
                    |           |
                    v           v
                  FAILED <---- FAILED
```

Power loss가 어느 write 경계에서 발생해도 이전 committed generation과 active confirmed
image를 식별할 수 있어야 한다.

## Confirmation

Boot Protocol은 OS-specific confirmation payload를 정의할 수 있다. Generic update
policy는 slot ID, metadata generation, boot nonce, manifest sequence, integrity result를
검증한다. 이전 boot의 confirmation replay와 torn write는 성공으로 처리하지 않는다.

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
