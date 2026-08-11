---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-08-11
code_paths:
  - include/Ribon/policy/ribos_extension.h
  - src/plugins/policy/ribos/extension.c
  - language/ribos/schema/
  - language/ribos/vm/
  - tools/install_sdk.py
  - examples/ribos-extension/
tests:
  - make check-ribos-extension-sdk
  - make check-sdk-surface
  - make check-sdk-reproducible
hardware:
  - none
supersedes:
  - source-tree-only libribon-policy-ribos packaging
---

# ADR-0058: Ribos typed extension ABI를 installed SDK로 승격

## 결정

Ribon SDK는 `libribon-policy-ribos.a`, `libribos-target-core.a`와 target-safe
`include/ribos/` header를 설치한다. 외부 package는
`RibonRibosExtensionDescriptor` 하나로 source signature schema, target execution
contract와 product-local semantic route를 결합한다.

Schema와 execution descriptor는 각각 canonical SHA-256 identity를 가진다. Callback
주소와 product context pointer는 canonical encoding과 digest에서 제외한다. Stable helper
ID, exact parameter/result type, capability, effect, typestate transition, operation bound와
product phase는 descriptor validation에서 field-wise로 다시 대조한다.

Dynamic library loading, arbitrary C FFI, raw pointer, raw MMIO와 raw flash helper는 이
ABI에 포함하지 않는다. Extension은 build-time product selection으로만 들어오며 runtime
discovery나 compatibility alias를 제공하지 않는다.

## 이유

Source tree 안에서만 adapter와 VM을 링크할 수 있으면 외부 firmware 개발자는 public
contract를 사용하면서도 Ribon checkout의 private layout에 종속된다. 반대로 callback을
직접 직렬화하거나 임의 FFI를 허용하면 artifact identity와 capability 검증이 process마다
달라진다. Semantic schema와 process-local execution route를 분리하면 C mechanism을
재사용하면서도 verifier-visible 의미를 고정할 수 있다.

## 기각한 대안

- `.so` 또는 runtime plugin loading: relocation, authenticity, W^X와 lifetime authority가
  현재 boot trust model 밖이므로 기각한다.
- raw C symbol import: signature와 capability를 verifier가 설명할 수 없으므로 기각한다.
- callback pointer를 schema에 기록: ASLR과 link layout이 product identity를 바꾸므로
  기각한다.
- source checkout include path 공개: installed SDK의 reproducibility와 private boundary를
  깨므로 기각한다.

## 결과

외부 package는 설치된 header와 여섯 target archive만으로 typed helper descriptor를
빌드하고 canonical schema artifact를 쓰고 읽을 수 있다. 이 결정은 native hardware,
dynamic code loading, arbitrary driver execution 또는 production secure boot 성공을
주장하지 않는다.
