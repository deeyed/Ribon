---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - include/Ribon/policy/ribos.h
  - src/plugins/policy/ribos/adapter.c
  - tools/generate_plugin_registry.py
  - qstar/schemas/product.schema.json
  - qstar/manifests/host-reference.json
  - tests/policy/ribos_integration_tests.c
tests:
  - make check-ribos-ribon-integration
  - make check-ribos-product-graphs
  - make check-ribos-normal-no-network
  - make check-ribos-factory-recovery
  - make check-object-graphs
  - make check
  - make docs
hardware:
  - none
supersedes:
  - unbound Ribos target-core VM
---

# ADR 0035: Ribos VM을 generated Ribon product binding으로 통합

## Context

Ribos target core는 artifact verification, prepared program, bounded storage,
interpreter, typed helper와 terminal outcome을 architecture-neutral archive로
완결했다. 그러나 Ribon product graph의 service, mode, phase, arena와 boot
transaction에 연결하는 production boundary는 없었다. Host replay fixture는 VM을
실행했지만 Ribon Core lifecycle이나 generated product authority를 소비하지 않았다.

VM 자체에 Ribon service ID, boot transaction 또는 product callback을 넣으면 언어
runtime이 특정 bootloader 구현에 종속된다. 반대로 product마다 handwritten switch를
두면 compiler, verifier와 runtime이 서로 다른 helper schema를 소비하고 normal mode의
network/flash 금지를 graph에서 증명할 수 없다.

## Decision

- `libribos-target-core.a`는 Ribon header와 symbol을 import하지 않는다.
- `libribon-policy-ribos.a`만 Ribon arena, plugin registry, typed service directory,
  watchdog와 boot transaction을 안다.
- Product manifest의 `ribos_policy`가 schema provider, stable helper execution
  descriptor, service route, capability, phase, mode와 resource limit을 선언한다.
- Composer는 schema identity와 canonical helper-contract digest를 결박한
  `RibonRibosProductBinding`을 generated registry에 만든다.
- Adapter는 compiler metadata를 신뢰하지 않고 selected policy plugin, product ID,
  schema digest, helper digest, route 정렬, service capability와 budget을 다시 검사한다.
- Normal-mode product binding에는 network, flash와 inactive-slot writer route를 넣지
  않는다. Recovery와 provisioning은 향후 별도 graph와 별도 binding으로 선택한다.
- Product root-of-trust callback이 artifact signature, key와 rollback을 승인한다.
  Generic adapter와 VM은 production key policy를 구현하지 않는다.
- Adapter는 Core arena를 authorization, prepared program, runtime storage와 handle
  table로 단방향 분할하며 rewind나 heap allocation을 허용하지 않는다.
- Watchdog가 required인 binding은 VM 실행 전에 selected typed service로 arm한다.
  Timer, instruction, helper, stack, handle와 deadline limit은 product limit과 artifact
  closure 중 더 좁은 값으로 집행한다.
- Sealed `BootAction`은 product callback으로 의미를 재검사한 뒤 한 번 consume한다.
  그 다음 기존 `RibonBootTransaction`의 commit과 environment quiesce를 정확히 한
  번 호출한다. Adapter는 transfer나 OS entry jump를 수행하지 않는다.
- PolicyError, VmFault, authorization, verification, action rejection, commit과
  quiesce failure는 외부 `.rba`가 필요 없는 compiled factory-recovery callback에
  최대 한 번 통지한다.

## Consequences

- 같은 target-core VM을 Ribon 외 embedder에서도 사용할 수 있다.
- Product schema와 helper dispatch가 한 generated graph에서 나오므로 callback
  주소와 무관한 canonical identity를 compiler, verifier와 adapter가 공유한다.
- Ribon Core의 service와 transaction ownership을 유지하면서 policy는 semantic
  operation만 선택한다.
- Host reference authorizer는 unsigned artifact를 허용하는 unit fixture다. Production
  signature, secure storage와 rollback 증거가 아니다.
- Host object graph와 transaction fixture에 더해 별도 diagnostic product에서 AMD64,
  AArch64와 RISC-V 64 QEMU guest 실행 증거가 있다. 이 증거는 policy adapter의
  transaction commit까지 다루며 OS transfer와 physical hardware는 다루지 않는다.
- Policy adapter archive는 source product graph의 구성요소다. Installed SDK archive
  집합에 포함시키는 packaging 결정은 별도 계약으로 남긴다.

## 기각한 대안

### VM core가 Ribon service directory를 직접 import

Architecture-neutral embedder 경계를 깨고 VM verifier/runtime의 object graph에
bootloader 의미론을 넣으므로 기각한다.

### Helper stable ID를 handwritten switch로 dispatch

Product schema, capability와 실제 callback이 쉽게 불일치하고 graph digest를 재현할
수 없으므로 generated route를 사용한다.

### Policy가 transaction commit 또는 control transfer를 직접 수행

Single-consume intent와 irreversible boot lifecycle의 authority가 중복되므로
기각한다. Policy는 action을 반환하고 기존 transaction만 commit과 quiesce를 소유한다.

### Normal binding에 dormant network와 flash callback을 링크

Runtime flag 결함이 recovery/update authority를 열 수 있으므로 object graph에서
제거한다.
