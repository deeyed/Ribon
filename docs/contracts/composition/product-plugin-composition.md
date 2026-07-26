---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-26
code_paths:
  - qstar.lua
  - qstar/
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
| `products/*/manifests/*.json` | product ID, 정확한 frontend tuple, plugin set과 limit |
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
- product descriptor
- selected object manifest
- final link map
- target artifact 또는 package manifest

Generated output은 정본 source가 아니며 build directory 밖에 기록하지 않는다.

## 정확한 provider 수

Product는 다음 provider 수를 만족한다.

| Provider | 수량 |
| --- | ---: |
| architecture backend | 1 |
| entry environment | 1 |
| platform facts | 1 |
| selected mode object | 1 |
| stable plugin ID | 1 이하 |

Boot manager product는 하나 이상의 Boot Protocol을 정적으로 포함할 수 있으나 한 boot
session은 정확히 하나를 선택한다. 선택되지 않은 protocol은 실행 중 검색하거나
동적으로 로드하지 않는다.

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
- firmware personality 없는 product의 runtime service
