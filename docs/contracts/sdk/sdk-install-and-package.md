---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-27
code_paths:
  - include/Ribon/sdk/
  - src/plugins/sdk.c
  - sdk/
  - examples/
  - tools/install_sdk.py
  - qstar/schemas/
tests:
  - ribon-sdk-install-surface
  - ribon-sdk-reproducible
  - ribon-out-of-tree-library-embed
  - ribon-external-plugin-contract
hardware:
  - none
supersedes:
  - source-tree-only plugin development
---

# SDK 설치와 외부 Plugin Package 계약

Ribon SDK는 source checkout 내부 include path를 요구하지 않는 세 library와 public
header surface다.

```text
include/Ribon/
lib/libribon-core.a
lib/libribon-boot.a
lib/libribon-sdk.a
lib/pkgconfig/ribon-sdk.pc
share/ribon/sdk-manifest.json
share/ribon/schemas/
share/ribon/templates/
```

`libribon-sdk`는 plugin package ABI, host contract harness, firmware personality와
personality-private service directory를 제공한다. Product가 선택한 architecture,
environment, protocol, port implementation은 SDK archive에 포함하지 않는다.

## ABI tuple

`RibonSdkAbiDescriptor`는 SDK, Core, Plugin, Firmware Personality와 source release
version을 하나의 immutable tuple로 제공한다. External package는 compile할 때 사용한
header tuple과 link된 archive tuple이 다르면 초기화 전에 실패해야 한다.

ABI symbol allowlist는 SDK major마다 별도 파일로 고정한다. Archive가 allowlist에 없는
global symbol을 추가하거나 기존 symbol을 제거하면 ABI review 없이 설치 gate를 통과할
수 없다.

## External package

Plugin package는 다음 source-owned 파일을 모두 제공한다.

- `package.json`: package ID, plugin ID와 kind, SDK ABI, file inventory
- `plugin.qst`: static library와 dependency graph
- public header: operation table과 exported descriptor
- source: immutable `RibonPluginDescriptor`, service package인 경우
  `RibonServiceDescriptor`, 그리고 typed validator
- contract test: positive graph와 malformed descriptor negative case
- documentation: ownership, budget, lifetime과 non-claim

Package source는 설치된 `include/Ribon/`만 소비한다. `src/`, `ports/`, `products/`
header와 generated registry를 include하지 않는다.

## Host contract harness

Host harness는 package descriptor, plugin kind, provided capability와 forbidden dependency를
검사한다. Harness 성공은 native firmware나 hardware 동작 증거가 아니다. Firmware
native ABI, MMIO, interrupt와 image 실행은 해당 target의 독립 evidence를 요구한다.

## 재현성

같은 public header, archive, schema와 template 입력은 byte-identical install tree와
동일한 SHA-256 file manifest를 생성해야 한다. SDK ABI 3은 Core ABI 3, Plugin ABI major 3와
bounded boot transaction 및 typed service directory public header를 함께 고정한다. Install manifest에는 timestamp,
checkout 절대 경로와 host-specific 작업 directory를 기록하지 않는다.
