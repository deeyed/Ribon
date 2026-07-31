---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - include/Ribon/core/
  - include/Ribon/boot/
  - include/Ribon/plugin/
  - include/Ribon/protocol/
  - include/Ribon/firmware/
  - include/Ribon/sdk/
  - include/Ribon/arch/
  - include/Ribon/port/
  - include/Ribon/policy/
tests:
  - ribon-public-api-lint
  - ribon-doxygen
  - ribon-docs
hardware:
  - none
supersedes:
  - flat public header API
---

# 공개 C API 구성

Ribon public API는 library consumer, plugin author, product composer가 소비하는 stable
header만 노출한다. Board, target, native firmware binding, OS wire implementation은 public
generic API가 아니다.

## Core API

```text
Ribon/core/context.h
Ribon/core/memory.h
Ribon/core/capability.h
Ribon/core/status.h
```

Core API는 caller-owned arena, descriptor validation, lifecycle state, normalized memory를
제공한다. Entry, firmware, OS protocol symbol을 선언하지 않는다.

## Boot Library API

```text
Ribon/boot/source.h
Ribon/boot/image.h
Ribon/boot/plan.h
Ribon/boot/transfer.h
Ribon/config/boot_config.h
Ribon/filesystem/fat32.h
Ribon/storage/block.h
```

Boot API는 caller-owned source, component, load plan, `RibonBootTransaction`, failure receipt와
prepare/commit/quiesce/terminal transfer를 제공한다. 특정 handoff wire field를 선언하지 않는다.

Boot media API는 read-only block geometry, GPT/MBR 검증, bounded FAT32 8.3 reader와
configuration candidate parser를 제공한다. Update writer, network transport와 native UEFI file
handle은 이 public generic API에 포함하지 않는다.

## Plugin SDK API

```text
Ribon/plugin/descriptor.h
Ribon/plugin/registry.h
Ribon/plugin/phases.h
Ribon/plugin/manifest.h
```

Plugin descriptor는 size와 ABI version으로 확장한다. Plugin implementation은 generated
registry를 수정하거나 hidden constructor로 자신을 등록하지 않는다.

## Installed SDK API

```text
Ribon/sdk/abi.h
Ribon/sdk/package.h
Ribon/sdk/host.h
```

SDK API는 immutable ABI tuple, external package inventory와 host-side contract
validation을 제공한다. Installed SDK consumer는 source-private header와 target generated
registry를 include하지 않는다.

## Boot Protocol API

```text
Ribon/protocol/protocol.h
Ribon/protocol/entry_contract.h
Ribon/protocol/confirmation.h
```

Parus, Linux, FreeBSD, Multiboot와 chainload 구현은 이 API를 소비한다. 각 protocol의
wire header는 해당 protocol package에 둔다.

## Service API

```text
Ribon/service/directory.h
```

Service API는 immutable typed descriptor, caller-owned directory, authority/collection selection과
operation lifetime을 제공한다. Monolithic service table, compatibility alias와 runtime locator는
public ABI에 없다.

## Ribos Policy Adapter API

```text
Ribon/policy/ribos.h
```

Policy adapter API는 generated product binding, semantic helper route, artifact
authorization, factory recovery, BootAction validation과 pointer-free execution receipt를
제공한다. Adapter만 Ribon arena, typed service와 boot transaction을 알고 Ribos VM public
type은 forward declaration으로 유지한다.

Product callback은 stable semantic helper만 구현하며 raw MMIO, raw flash와 arbitrary
transfer를 노출하지 않는다. `ribon_ribos_policy_execute`는 sealed BootAction을
재검사·single-consume한 뒤 기존 transaction commit과 quiesce를 수행하지만 architecture
transfer를 호출하지 않는다.

## Firmware API

```text
Ribon/firmware/environment.h
Ribon/firmware/personality.h
```

Environment consumer와 firmware personality descriptor는 서로 다른 kind를 사용한다.
UEFI native declaration과 BIOS register frame은 environment 또는 personality-private
header에 둔다. Firmware service directory는 caller-owned storage이며 selected
personality lifetime을 벗어나지 않는다.

## Architecture API

```text
Ribon/arch/ops.h
Ribon/arch/entry.h
```

Architecture operation은 format별 machine fact, canonical address, cache, privilege,
transfer를 제공한다. Image parser는 이 descriptor를 소비하지 않고 구조와 machine
field만 추출한다. Board resource와 OS permanent page table을 노출하지 않는다.

## Port API

```text
Ribon/port/port.h
```

Port descriptor는 target recipe가 선택한 architecture/environment tuple와 independent
diagnostic, machine-description, payload-placement service descriptor를 묶는다. Core와
Boot Library는 aggregate port descriptor를 소비하지 않는다. Runtime-discovered FDT
또는 firmware fact와 service constraint가 충돌하면 environment capture가
fail-closed한다. Port API는 OS wire artifact를 포함하지 않는다.

## ABI 규칙

- Public struct는 `size`와 ABI version을 가진다.
- Native pointer를 generic ABI에 넣지 않는다.
- Wire artifact는 byte-wise serialization하며 C layout을 사용하지 않는다.
- Enum과 bit 번호는 계약 없이 재사용하지 않는다.
- Callback capability와 pointer 존재 여부는 일치해야 한다.
- Allocation, ownership, lifetime, interrupt state, deadline을 Doxygen에 기록한다.
- Compatibility typedef, alias function, dual registry를 두지 않는다.

## Doxygen과 Breathe

Public header가 구현되면 모든 type, enum, macro, function에 한국어 Doxygen 계약을
작성한다. Doxygen XML과 Breathe가 이 페이지의 하위 API reference를 생성한다.
Target-private와 vendored firmware header는 generic public API project에 포함하지 않는다.
