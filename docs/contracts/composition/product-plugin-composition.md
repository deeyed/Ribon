---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-26
code_paths:
  - qstar.lua
  - qstar/
  - products/
  - targets/
  - build/generated/
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

QStar는 Ribon component graph의 정본 build composer다. Makefile은 개발 편의 wrapper일
수 있지만 source ownership, plugin dependency, target identity를 별도로 정의하지 않는다.

## Metadata 계층

| Metadata | 역할 |
| --- | --- |
| `plugin.qst` | 한 plugin의 ABI, source, capability, dependency |
| `package.qst` | 외부에 공개할 header, plugin, license 묶음 |
| `product.qst` | bootloader 또는 firmware 기능 조합 |
| `target.qst` | architecture, environment, platform, product 선택 |
| `image.qst` | linker, header, padding, signing, package recipe |

한 metadata 계층의 값을 C preprocessor macro로 중복 정의하지 않는다.

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

Composer는 최소 다음을 `build/generated/` 아래 생성한다.

- plugin registry
- product descriptor
- capability and phase report
- selected object manifest
- public ABI compatibility report
- image input manifest

Generated output은 정본 source가 아니며 build directory 밖에 기록하지 않는다.

## 정확한 provider 수

Product는 다음 provider 수를 만족한다.

| Provider | 수량 |
| --- | ---: |
| architecture backend | 1 |
| entry environment 또는 firmware personality root | 1 |
| boot policy | 1 |
| selected mode policy | 1 |
| stable plugin ID | 1 이하 |

Boot Protocol은 menu 또는 product policy가 허용한 수만큼 포함할 수 있다. 선택되지 않은
protocol은 실행 중 검색하거나 로드하지 않는다.

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
