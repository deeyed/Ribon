---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-27
code_paths:
  - include/Ribon/service/
  - include/Ribon/core/
  - include/Ribon/plugin/
  - src/core/
  - src/environments/
  - tools/generate_plugin_registry.py
  - products/
  - qstar/manifests/
tests:
  - make check-core-service
  - make check-plugin-descriptors
  - make check-library-embed
  - make check-external-plugin
hardware:
  - none
supersedes:
  - monolithic RibonServiceTable ABI
---

# ADR: Typed Service Graph로 monolithic service ABI를 hard cut한다

## 맥락

기존 complete service table은 서로 다른 native lifetime과 authority를 하나의 callback
struct에 묶었다. 이 구조는 boot source, timer, update storage, network transport와 future
filesystem을 같은 provider로 가정하고, 둘 이상의 static protocol/image/transport provider가
있는 product에서 active owner의 근거를 표현하지 못한다.

Ribon은 generic library이므로 environment, board, OS protocol이 Core ABI에 service locator나
native handle을 추가해서는 안 된다. 동시에 QStar product graph는 compile time에 provider
set과 선택을 고정해야 한다.

## 결정

1. `RibonServiceTable`, initializer, validator, header path와 compatibility alias를 삭제한다.
2. `RibonServiceDescriptor`는 stable ID, typed role, capability, phase, lifetime,
   compatibility mask, budget, operation ABI와 typed validator를 가진다.
3. QStar manifest의 `services` field가 descriptor symbol을 stable ID 순으로 열거하고,
   generator가 immutable `RibonServiceDirectory`를 만든다.
4. Authority role은 한 provider만 허용한다. Collection role은 여러 static provider를
   허용하지만 둘 이상이면 `service_selections`가 active stable ID를 고정한다.
5. Plugin capability dependency도 collection provider를 암묵 선택하지 않는다.
   둘 이상이면 `plugin_selections`가 exact kind와 ID를 고정해야 한다.
6. Core context는 registry와 service directory를 callback, allocation, discovery 없이
   검증한다. Boot session은 별도 environment table 인자를 받지 않고 validated context의
   directory만 참조한다.
7. SDK ABI 2, Core ABI 3, Plugin ABI major 3을 동시에 적용한다. 이전 ABI wrapper,
   dual directory 또는 legacy install allowlist를 제공하지 않는다.

## 실패 규칙

다음은 fail-closed다.

- duplicate stable service ID 또는 authority role
- required authority 부재
- unselected collection ambiguity
- dependency cycle 또는 provider phase inversion
- operation descriptor size, ABI, role/capability 불일치
- product mode, architecture/environment tuple 또는 resource budget 위반

## 결과

향후 filesystem, transport, OTA storage, watchdog, diagnostic provider는 generic typed
service role과 product manifest로 조합한다. Parus overseer, RPH1, OS health policy는 이
directory에 special case를 추가하지 않으며 OS-specific protocol/policy package에 남는다.
