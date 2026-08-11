---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-08-11
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
  - make check-sdk-deployment-consumer
  - make check-deployment-release-reproducibility
hardware:
  - none
supersedes:
  - source-tree-only plugin development
---

# SDK 설치와 외부 Plugin Package 계약

Ribon SDK는 source checkout 내부 include path를 요구하지 않는 여섯 target library,
public header surface와 명시적인 host-only tool 집합이다.

```text
include/Ribon/
include/ribos/schema/
include/ribos/artifact/
include/ribos/vm/
lib/libribon-core.a
lib/libribon-boot.a
lib/libribon-sdk.a
lib/libribon-update.a
lib/libribon-policy-ribos.a
lib/libribos-target-core.a
lib/pkgconfig/ribon-sdk.pc
bin/ribosc
bin/ribos-verify
bin/ribos-run
bin/ribon-compose-product
bin/ribon-update-manifest
bin/ribon-update-layout
bin/ribon-sign-policy
share/ribon/sdk-manifest.json
share/ribon/schemas/
share/ribon/templates/
```

`libribon-sdk`는 plugin package ABI, host contract harness, firmware personality와
personality-private service directory를 제공한다. Product가 선택한 architecture,
environment, protocol, port implementation은 SDK archive에 포함하지 않는다.

`libribon-update`는 canonical update manifest reader, bounded storage와 transaction,
boot confirmation, key-policy와 protected-state abstraction을 제공한다. Ed25519 private-key
연산, concrete signature provider, Monocypher implementation과 fixture key는 archive에 포함하지
않는다. `ribon-sign-policy`만 offline private-key capable host tool이며 target-linkable artifact가
아니다.

## 설치 manifest v2

`ribon-sdk-install-v2`는 source revision과 source version, SDK/Core/Plugin ABI, 각 파일 SHA-256,
host tool class와 다음 boundary를 기록한다.

- host tool은 target-linkable이 아니다.
- 설치 tree에는 private key material이 없다.
- target library 집합은 여섯 archive로 exact-match한다.

설치된 tool은 product registry 생성, Ribos compile·독립 verify·host replay, update layout과
manifest 조립·검사, offline policy signing을 담당한다. Product template는 설치 tree의
`share/ribon/templates/`에서 제공한다.

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

같은 public header, archive, schema와 template 입력은 서로 다른 clean build root에서도
byte-identical install tree와 동일한 SHA-256 file manifest를 생성해야 한다. Compiler debug
path, archive timestamp, generated report의 source 절대 경로와 Darwin UUID는 canonical release
identity에 들어가지 않는다. SDK ABI 6은 Core ABI 7, Plugin ABI major 6과 typed Ribos
extension surface를 함께 고정한다. Install manifest에는 timestamp, checkout 절대 경로와
host-specific 작업 directory를 기록하지 않는다.

`sdk/templates/deployment-consumer`의 recovery/update product는 설치된 tool, schema, public
header와 archive만 사용해 out-of-tree에서 product graph, `.rbs` artifact, update manifest와
host executable을 만든다. Dependency file에 install root와 consumer root 밖의 header가 하나라도
나타나면 gate는 실패한다. 이 성공은 host build/unit evidence이며 target firmware 실행 증거가 아니다.
