---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - Makefile
  - targets/targets.qst
  - products/bootmgr/manifests/x86_64-uefi-parus-fixture.json
  - products/bootmgr/manifests/x86_64-uefi-parus-external.json
  - tools/check_uefi_product_hermeticity.py
  - tools/validate_external_parus_payload.py
tests:
  - make check-uefi-product-hermeticity
  - make x86_64-uefi-parus-fixture-smoke
  - make check-target-builds
hardware:
  - none
supersedes:
  - environment-selected shared x86_64 UEFI output root
---

# ADR: UEFI build output을 product identity로 격리한다

## 맥락

x86_64 UEFI fixture와 external-Parus build가 같은 application, generated registry, object,
ESP와 result path를 사용하면서 external payload selector가 선택 manifest와 payload dependency를
바꾸었다. Make는 target path가 같으면 이전 recipe 의미를 product identity로 구분할 수 없으므로,
fixture 뒤 external 또는 external 뒤 fixture를 빌드할 때 이미 존재하는 output이 재사용될 수 있다.

Payload-class preflight는 잘못 조합된 결과를 실행 전에 거부할 수 있지만, shared output 자체를
허용하면 build 순서와 local tree history가 artifact 의미에 포함된다. 이는 product manifest를
조합의 정본으로 삼는 계약과 재현 가능한 firmware image 요구를 위반한다.

## 결정

1. Fixture와 external-kernel UEFI build를 별도 product ID와 명시적 Make target으로 선택한다.
2. 각 product는 application, registry, object, map, ESP, copied manifest와 result를 포함하는
   고유 build root를 가진다.
3. External payload selector는 external target의 input일 뿐 fixture target의 manifest, dependency
   또는 output을 바꾸지 않는다.
4. External product는 payload copy 전에 manifest tuple, ELF load window, fixture marker 부재와
   digest를 검증하고 product-local result로 보존한다.
5. 양방향 증분 순서와 독립 build root를 자동 실행하여 반대 product 불변성과 canonical output
   재현성을 검사한다.
6. 이전 `x86_64-uefi-app`과 `x86_64-uefi-parus-smoke` target은 compatibility alias 없이 제거한다.

## 불변식

```text
product_id A != product_id B
    => writable_output_root(A) ∩ writable_output_root(B) = ∅

same(product_id, manifest, external_input_digest, toolchain)
    => same(canonical composed artifact)
```

Source tree의 `targets/x86_64-uefi-app/`는 UEFI application entry 구현을 소유하는 source path이며
product output identity가 아니다. Firmware Personality provider와 UEFI consumer도 서로 다른
dependency 방향을 유지한다.

## 결과와 증거 경계

새 product target은 stale ESP와 registry 재사용을 구조적으로 차단하고 external payload 교체를
dependency graph에 반영한다. Hermeticity host gate의 synthetic external-input ELF는 build
isolation만 검사하며 실제 Parus kernel, OS terminal receipt 또는 UEFI conformance 증거가 아니다.
실제 external runtime 주장은 별도 supervised QEMU 실행과 Parus marker graph를 요구한다.

## 기각한 대안

- Shared directory를 유지하고 recipe에서 payload를 항상 copy하는 방식은 registry, object, map과
  result의 product identity를 여전히 공유하므로 기각한다.
- Build 전에 shared directory를 삭제하는 방식은 incremental build를 파괴하고 undeclared state를
  감추므로 기각한다.
- 이전 target을 alias로 유지하는 방식은 어떤 product를 선택하는지 모호하게 하므로 기각한다.
