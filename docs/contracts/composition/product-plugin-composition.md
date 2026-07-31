---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - include/Ribon/policy/ribos.h
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
  - make check-ribos-product-graphs
  - make check-ribos-normal-no-network
  - make check-uefi-product-hermeticity
  - make check-boot-modules
  - make check-security-provider-graphs
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
typed port service set
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

Security binding을 생성하는 product의 trust identity는 selected source manifest exact bytes의
SHA-256이다. Composer report의 source-manifest digest와 signer input의 product digest는 같아야
한다. Parsed JSON을 다시 serialize한 byte나 generated C source hash로 product identity를
대체하지 않는다.

Ribos policy를 선택한 product는 추가로 versioned schema provider, stable-ID helper
execution table, exact service route, canonical helper-contract digest, mode/phase,
single key usage, rollback-domain digest와 resource limit을 생성한다. Compiler/verifier가
소비하는 schema identity와 adapter가 dispatch하는 helper contract는 같은 manifest에서
나와야 한다. Callback 주소와 native pointer는 canonical digest에 포함하지 않는다.

Generated output은 정본 source가 아니며 build directory 밖에 기록하지 않는다.

Signed object를 승인하는 product는 `signature_provider`에 algorithm, production/fixture class,
stable provider ID와 descriptor symbol을 정확히 하나 선택한다. Composer는 provider pointer와
source-manifest exact-byte digest를 같은 generated registry에 낸다. Production graph는 fixture
class를 선택하거나 production/fixture callback을 함께 fallback closure로 링크할 수 없다.
QStar는 generic signature library와 concrete Ed25519 provider library를 별도 target으로 유지하고,
product security suite가 provider unit과 final generated product graph를 함께 선택한다.

### Build-time component bundle

Build-time component를 embedded publication하는 product는 component 입력과 generated object를
manifest 밖의 Make 변수만으로 활성화해서는 안 된다. Module-bearing raw-FDT product는
`boot_module_bundle`, exact `boot-module-bundle` authority service와 required/allowed
`BOOT_MODULE_BUNDLE` capability를 모두 선택한다. 이 네 항목은 양방향 일관성을 만족해야 한다.

Module-free product에는 bundle metadata, service, capability와 generated/module support object가
모두 없어야 한다. Linker의 빈 canonical section symbol은 provider로 세지 않는다. Component
generator는 selected product manifest digest를 provenance에 봉인하고 product-owned build root
밖에 snapshot 또는 generated source를 기록하지 않는다.

### Product별 output identity

서로 다른 `product_id`를 가진 target은 같은 writable output root를 공유하지 않는다.
Application, generated registry, object, link map, package/ESP, copied manifest, payload,
configuration과 result는 product-owned root 아래에만 생성한다. Environment variable 또는
external payload 존재 여부가 이미 선택된 product root의 manifest, object graph나 artifact
의미를 바꿀 수 없다.

External input을 받는 product는 source path에만 의존하지 않고 input digest와 class를
product result에 기록한다. Input path 또는 bytes가 바뀌면 validation, copied payload와
composed artifact가 다시 생성되어야 한다. Fixture와 external product를 어느 순서로
증분 빌드해도 반대 product의 canonical output은 변하지 않아야 하며, 독립 build root의
동일 product/input은 같은 canonical artifact를 생성해야 한다.

## 정확한 provider 수

Bootloader product는 다음 provider 수를 만족한다.

| Provider | 수량 |
| --- | ---: |
| architecture backend | 1 |
| entry environment | 1 |
| selected mode object | 1 |
| stable plugin ID | 1 이하 |
| signature provider | signed object product에서 1, 그 외 0 또는 1 |

Port service는 target이 실제로 필요한 role만 0개 이상 제공한다. 각 role의
authority/collection cardinality는 service directory가 독립적으로 검증하며 generic
Core가 한 개의 board 또는 platform plugin 존재를 요구하지 않는다.

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
environment와 service tuple을 명시한다. Library에 포함된 service package도 generated
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
- normal Ribos binding의 network, flash 또는 update-writer helper route
- Ribos schema/helper digest 불일치와 unsorted/duplicate helper route
- firmware personality 없는 product의 runtime service
- service authority 중복, collection owner 미선택, service ABI/lifetime/mode budget 불일치
- external-media target의 runtime object graph에 embedded payload fixture object
- module-free raw-FDT target의 module bundle service, capability 또는 module object
- module-bearing raw-FDT target의 product authority와 generated provider 불일치
- production signed-object target의 fixture provider, signer symbol 또는 private-key material
