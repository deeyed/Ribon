---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-07-31
code_paths:
  - include/Ribon/policy/ribos.h
  - src/plugins/policy/ribos/adapter.c
  - src/environments/host/ribos_policy.c
  - tools/generate_plugin_registry.py
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
  - not-run
supersedes:
  - none
---

# Ribos와 Ribon product 통합 구현 기록

## 구현

Generic `policy.ribos` plugin과 `libribon-policy-ribos.a`를 추가했다. Adapter만 Ribon
Core arena, typed service directory, watchdog와 `RibonBootTransaction`을 소비하며
`libribos-target-core.a`의 Ribon symbol import 금지는 유지했다.

Product schema에 `ribos_policy`를 추가하고 composer가 helper execution table, service
route, canonical helper-contract digest, resource limit와 callback binding을 generated
registry에 만들도록 했다. Host reference는 normal-mode fixture로 inspect/boot helper,
timer와 watchdog만 선택하며 network, flash와 update writer는 포함하지 않는다.

Host product semantics는 unit-only artifact authorizer, semantic helper, BootAction
validator와 external artifact 없이 실행하는 factory recovery를 제공한다. 이 fixture의
unsigned 승인 경로는 production root-of-trust 구현이 아니다.

Adapter는 binding과 service budget을 다시 검사하고 Core arena에서 authorization,
prepared program, runtime과 handle storage를 단방향 할당한다. VM의 sealed BootAction을
제품 의미로 재검사하고 consume한 뒤 기존 transaction commit과 quiesce를 각각 한 번
호출한다. Adapter는 transfer를 수행하지 않는다.

## 검증

- real `.rbs` compiler output에서 generated product binding까지 positive 실행
- semantic helper 네 개와 opaque handle transition
- BootAction single consume
- persistent metadata write/flush와 environment quiesce 각 1회
- required watchdog arm
- unsigned rejection과 corrupt artifact
- alternate schema digest mismatch
- instruction budget과 deadline fault
- BootAction product rejection
- action consume 뒤 commit failure의 fail-closed recovery
- external `.rba` 부재에서 factory recovery
- generated product graph 결정성과 hostile manifest 거부
- normal graph의 network/flash/update authority 부재
- VM archive의 Ribon import 부재와 adapter object graph 격리

## 증거 한계

이 기록은 host object, generated binding과 transaction fixture 증거다. Production
signature와 rollback, cross-target firmware link, QEMU transfer, recovery networking,
OTA flash와 physical hardware는 실행하지 않았다.
