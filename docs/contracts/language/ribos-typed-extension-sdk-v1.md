---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-08-11
code_paths:
  - include/Ribon/policy/ribos_extension.h
  - src/plugins/policy/ribos/extension.c
  - examples/ribos-extension/
  - sdk/abi/libribon-policy-ribos-v1.symbols
tests:
  - make check-ribos-extension-sdk
  - make check-ribos-schema
  - make check-ribos-ribon-integration
  - make check-object-graphs
hardware:
  - none
supersedes:
  - private product-local Ribos helper extension
---

# Ribos typed extension SDK v1 계약

## Package 계층

외부 extension은 C mechanism, canonical semantic descriptor와 Ribos policy를 분리한다.

```text
product selection
  -> package ID
  -> RibosProductSchema
  -> RibosVmHelperContract
  -> RibonRibosHelperRoute
  -> RibonRibosExtensionDescriptor
```

`RibosProductSchema`는 helper의 stable ID, path, exact parameter/result/error type,
capability와 typestate annotation을 소유한다. `RibosVmHelperContract`는 같은 stable ID의
effect, durability, execution mode, handle transition, mode/phase mask, input/output,
operation/poll/time bound를 소유한다. `RibonRibosHelperRoute`만 C callback과 선택된 Ribon
service를 소유한다.

Product-local callback table은 target image에 정적으로 링크한다. Callback pointer,
service descriptor pointer와 product context는 schema artifact나 digest에 기록하지 않는다.

## Validation closure

`ribon_ribos_extension_validate_v1()`은 다음을 모두 만족할 때만 성공한다.

- package ABI와 모든 reserved field가 exact v1이다.
- schema, helper contract와 route count가 같고 1..256 범위다.
- 세 table의 stable ID가 같은 오름차순이며 duplicate가 없다.
- schema capability와 execution capability가 exact-match한다.
- helper capability가 product grant를 넘지 않는다.
- typestate flag, transition kind와 transition parameter가 일치한다.
- terminal boot annotation과 terminal effect가 일치한다.
- execution phase mask가 선택 product phase를 포함한다.
- helper call budget이 package helper 수보다 작지 않다.
- schema와 execution digest가 descriptor를 다시 계산한 값과 같다.

Capability 추가, signature 변경, ordering 변경과 stale digest는 모두 fail-closed다.
ABI v1은 compatibility wrapper나 old/new selection option을 제공하지 않는다.

## Schema artifact

Schema artifact는 `RBSCHM1`, little-endian integer와 length-prefixed UTF-8 byte로 구성된
기존 canonical Ribos schema encoding이다. Writer는 C layout과 pointer를 저장하지 않는다.
Reader는 artifact의 모든 field와 final length를 descriptor에 대조한다. Truncation, trailing
byte, overflow, reordered row와 changed string은 거부한다.

Compiler가 생성한 policy artifact의 schema digest, independent verifier가 받은 product
schema digest와 runtime extension schema digest는 동일한 canonical descriptor에서 계산해야
한다. 서로 다른 digest를 compatibility로 수용하지 않는다.

## Installed surface

Installed SDK는 다음 추가 surface를 제공한다.

```text
include/Ribon/policy/ribos_extension.h
include/ribos/schema/
include/ribos/artifact/
include/ribos/vm/
lib/libribon-policy-ribos.a
lib/libribos-target-core.a
```

Frontend, parser generator, IR builder와 host compiler library는 target SDK header/archive가
아니다. Target firmware는 `.rbs` parser나 compiler를 링크하지 않는다.

## Evidence와 non-claim

`make check-ribos-extension-sdk`는 installed-only compile/link, positive descriptor,
canonical schema round trip, ABI mismatch, capability widening, stale digest, route mismatch와
budget failure를 검증한다. 이는 host object evidence다. 실제 device initialization,
firmware personality boot, native target timing과 secure-boot production key는 별도 evidence가
필요하다.
