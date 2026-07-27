---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-27
code_paths:
  - qstar.lua
  - qstar/
  - qstar/schemas/
  - products/bootmgr/manifests/
  - targets/
  - tools/generate_plugin_registry.py
tests:
  - ribon-plugin-graph-lint
  - ribon-product-composition-test
  - ribon-object-graph-lint
hardware:
  - none
supersedes:
  - frontend source-list composition
---

# Product와 Plugin 조합 계약

Source-owned product manifest는 Ribon component graph의 정본 tuple이다. QStar는 해당
tuple과 target closure를 검사하고, Makefile target recipe는 같은 manifest에서 생성한
registry를 링크한다. C source list가 product identity를 암묵적으로 새로 정의해서는 안
된다.

## Metadata 계층

| Metadata | 역할 |
| --- | --- |
| `RibonPluginDescriptor` | 한 plugin의 ABI, capability, dependency와 budget |
| `RibonServiceDescriptor` | typed service role, lifetime, operation ABI와 budget |
| `products/*/manifests/*.json` | product ID, 정확한 frontend tuple, plugin set과 limit |
| `qstar/schemas/*.schema.json` | package, product, target, image metadata field와 type |
| `targets/*.qst` | QStar target closure와 generated artifact |
| `targets/<target>/` | native entry, linker, image와 package recipe |
| `qstar/*.qst` | library, plugin, test dependency graph |

Architecture-specific host fixture 외에는 product tuple을 C preprocessor macro로
중복 정의하지 않는다.

## Product tuple

Target은 다음 축을 명시한다.

```text
product
architecture
entry environment 또는 firmware personality
platform
boot protocol set
policy set
image recipe
evidence policy
```

임의 Cartesian product를 허용하지 않는다. QStar가 승인한 tuple만 image target이 된다.

## Generated output

Composer는 `build/` 아래 최소 다음을 생성한다.

- plugin registry
- typed service directory와 collection owner selection
- product descriptor
- selected object manifest
- final link map
- target artifact 또는 package manifest

Generated output은 정본 source가 아니며 build directory 밖에 기록하지 않는다.

## 정확한 provider 수

Bootloader product는 다음 provider 수를 만족한다.

| Provider | 수량 |
| --- | ---: |
| architecture backend | 1 |
| entry environment | 1 |
| platform facts | 1 |
| selected mode object | 1 |
| stable plugin ID | 1 이하 |

Boot manager product는 하나 이상의 Boot Protocol, image format, filesystem 또는 transport
provider를 정적으로 포함할 수 있으나 한 boot session은 manifest selection으로 정확히 하나의
active owner를 고정한다. 선택되지 않은 provider는 실행 중 검색하거나 동적으로 로드하지
않는다.

`services` manifest field는 provider source가 export한 service descriptor symbol을 stable ID
순으로 열거한다. `service_selections`는 collection role의 active descriptor ID를 ABI role
순으로 열거한다. Authority role은 selection을 사용하지 않으며 중복이면 composition이 실패한다.

Firmware product는 entry environment 대신 정확히 하나의 firmware personality를
선택한다. Personality가 선언한 `personality_mask`와 product tuple은 정확히 일치해야
하며 environment plugin은 포함하지 않는다.

Library product는 Boot Protocol provider가 없는 protocol-free embed를 허용한다.
Generated registry를 사용하는 host contract product는 실행할 architecture,
environment와 platform tuple을 명시한다. Library에 포함된 service package도 generated
registry와 ABI validator를 통과해야 한다.

## Plugin phase

Composer는 다음 순서를 위반하는 dependency를 거부한다.

```text
EARLY -> FOUNDATION -> DRIVER -> BOOT -> QUIESCE -> RUNTIME
```

`RUNTIME` provider는 firmware product에서만 허용하고 bootloader product에는 남기지
않는다.

## Hard-cut 규칙

이전 API와 target을 위한 compatibility archive, alias target, dual registry를 두지
않는다. 새 graph로 이전하지 않은 target은 명시적으로 제거하며 이름만 새 구조로 감싼
legacy source list를 허용하지 않는다.

## Object graph gate

Gate는 archive member와 final link map을 검사하여 다음을 거부한다.

- Core archive의 protocol, board, environment, personality object
- UEFI application target의 BIOS 또는 raw-FDT object
- QEMU `virt` target의 RPi5 object와 package input
- Linux 또는 FreeBSD product의 Parus object
- normal product의 recovery network와 update writer
- normal product의 mutable filesystem 또는 inactive destination storage authority
- firmware personality 없는 product의 runtime service
- service authority 중복, collection owner 미선택, service ABI/lifetime/mode budget 불일치
- external-media target의 runtime object graph에 embedded payload fixture object
