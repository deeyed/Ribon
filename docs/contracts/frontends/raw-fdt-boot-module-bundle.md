---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - include/Ribon/boot/module_bundle.h
  - include/Ribon/firmware/environment.h
  - src/common/module_bundle.c
  - src/environments/raw-fdt/
  - products/bootmgr/raw_fdt_main.c
  - tools/generate_boot_module_bundle.py
  - targets/qemu-aarch64-virt-raw-fdt/linker.ld
  - targets/qemu-riscv64-virt-opensbi/linker.ld
  - targets/rpi5-aarch64-raw-fdt/linker.ld
tests:
  - make check-boot-modules
  - make check-rph1
  - make qemu-aarch64-virt-modules-fixture-smoke
  - make qemu-aarch64-virt-parus-modules-smoke
  - make check-target-builds
hardware:
  - none
supersedes:
  - raw-FDT embedded-kernel-only module publication
---

# raw-FDT typed boot-module bundle 계약

raw-FDT boot manager는 build-time component assembly와 runtime typed publication을
분리한다. Component schema, materializer와 memory reservation은 architecture와 OS 이름을
알지 않는다. 최종 OS wire format 선택은 Boot Protocol plugin이 소유한다.

## Source component manifest

Module-bearing product는 다음 exact JSON schema의 source-owned 또는 external input manifest를
하나 제공한다.

```json
{
  "components": [
    {
      "expected_sha256": "64 lowercase hexadecimal characters",
      "expected_size": 4096,
      "maximum_size": 65536,
      "name": "initial-user",
      "role": "initial-image",
      "source": "components/initial-user.elf"
    }
  ],
  "schema": "ribon-boot-module-components-v1"
}
```

Root와 component object는 예시에 나온 key를 정확히 가져야 한다. Unknown 또는 누락 key는
거부한다.

| Field | 계약 |
| --- | --- |
| `components` | manifest 순서를 보존하는 1..8개 배열 |
| `name` | 1..63자의 ASCII alphanumeric, `.`, `_`, `-`; 중복 금지 |
| `role` | `initial-image` 또는 `auxiliary` |
| `source` | manifest 기준 POSIX 상대 경로; absolute, `.`/`..`, backslash, symlink 금지 |
| `expected_size` | 1 이상의 exact byte 수 |
| `maximum_size` | `expected_size` 이상인 product review 상한 |
| `expected_sha256` | 입력 exact bytes의 lowercase SHA-256 |

`initial-image`는 bundle당 최대 하나다. Auxiliary-only bundle은 유효하다. 빈 component
manifest는 만들지 않으며 module-free product가 0-module 의미를 표현한다.

Generator는 regular file을 exact read하고 read 전후 inode, size와 modification identity가
변하지 않았는지 검사한다. Short read, trailing byte, digest mismatch와 concurrent mutation은
fail-closed한다.

## Product graph authority

Component 입력만으로 module publication을 켤 수 없다. Module-bearing raw-FDT product는 다음
네 항목을 모두 정확히 선택해야 한다.

```text
boot_module_bundle.provider = generated-component-bundle-v1
service.product.boot-module-bundle
required_capabilities += BOOT_MODULE_BUNDLE
allowed_capabilities += BOOT_MODULE_BUNDLE
```

`boot_module_bundle`의 `component_manifest_schema`는
`ribon-boot-module-components-v1`, `maximum_modules`는 8이다. Service는 authority cardinality,
persistent lifetime, EARLY phase의 data-only provider다. Product composer, bundle generator와
raw-FDT entry가 이 iff 관계를 각각 검사한다.

Module-free product에는 위 네 항목과 generated descriptor, component snapshot,
`module_bundle.o`가 모두 없어야 한다. Linker가 canonical 빈 section symbol을 제공하는 것은
module provider 선택으로 간주하지 않는다.

## Deterministic generated bundle

Generator는 product-owned build root 밖에 쓰지 않으며 다음 산출물을 만든다.

```text
generated/boot-modules/bundle.S
generated/boot-modules/descriptor.c
generated/boot-modules/boot-module-components/000.bin ... 007.bin
results/boot-modules.json
```

Manifest 순서가 descriptor, section ordinal, provenance와 최종 RPH1 entry 순서다. Assembly는
각 snapshot을 `.ribon.boot_modules.NNN` read-only input section에 `.incbin`하고 각 slot의
시작과 끝을 4 KiB에 정렬한다. Descriptor는 exact byte span과 semantic role만 publication하며
raw path, OS 이름과 protocol flag를 포함하지 않는다.

Provenance schema는 `ribon-boot-module-bundle-provenance-v1`이다. 다음을 포함한다.

- `product_id`와 exact product manifest SHA-256
- `component_count`
- 순번별 `index`, `name`, `role`, `source`, `snapshot`, exact `size`, `maximum_size`, SHA-256
- schema tag, 순서, logical identity, role와 exact bytes를 결합한 `bundle_sha256`

Build root 아래 snapshot만 assembly 입력이 된다. Source path는 provenance이지 linker 입력
authority가 아니다.

## Linker와 physical lifetime

AArch64 QEMU, RPi5 AArch64와 RISC-V raw-FDT linker는 다음 symbol contract를 구현한다.

```text
[__image_start, __bootloader_runtime_end)       mutable Ribon runtime
[__ribon_boot_modules_start,
 __ribon_boot_modules_end)                     page-aligned module backing
__image_end                                    package에 포함되는 마지막 byte 이후
```

`.ribon.boot_modules.*`는 `SORT_BY_NAME`과 `KEEP`으로 canonical section에 모은다.
`__bootloader_runtime_end`는 module byte보다 앞에 있으므로 bootloader reservation이 module을
흡수해서는 안 된다. `__image_end`는 module byte까지 포함하며 target linker는 image가 kernel
payload placement window에 침범하면 link를 거부한다.

Semantic `RibonBootModule.size`는 file의 exact byte 수다. Memory map의
`RIBON_MEMORY_REGION_BOOT_MODULE` reservation은 해당 exact span을 포함하는 page-aligned backing
span이다. 두 수명을 혼동하거나 module을 bootloader-owned borrowed range로만 표시하지 않는다.

Materializer는 다음을 독립적으로 검사한다.

- bundle/service ABI, non-null exact span과 known role
- component가 canonical module section 안에 있고 page-aligned인지
- non-wrapping section, bootloader, kernel과 component range
- initial-image singleton
- bootloader runtime, kernel placement와 module/module backing overlap 부재
- stable logical name과 8개 capacity

Module array, name과 backing bytes는 final handoff 생성이 끝날 때까지 immutable persistent
input이다. raw-FDT capture 뒤 boot media, command line과 module list를
`ribon_boot_environment_apply_persistent_inputs()`로 다시 적용한 다음에만 environment를
검증하고 transaction을 시작한다.

## Bounded memory-map capacity

raw-FDT target reservation 상한은 magic number가 아니라 다음 식이다.

```text
target reservations = bootloader 1 + kernel placement 1 + modules 8 = 10
all reservations    = target reservations 10 + FDT 1 = 11
worst normalized regions for one memory bank = 2 * 11 + 1 = 23
```

23-entry storage는 이 최악 case를 수용해야 하고 22-entry storage는 deterministic
`OUT_OF_CAPACITY`를 반환해야 한다. Zero-size, wrapping 또는 서로 겹치는 reservation은
capture 전에 실패한다.

## Protocol projection

Generic boot transaction은 module이 존재할 때 selected protocol이
`RIBON_PROTOCOL_ALLOW_BOOT_MODULES`를 선언하지 않으면 거부한다. Component budget은 kernel
segment와 boot module 수를 함께 계산한다.

`protocol.parus`는 새 wire protocol을 만들지 않고 기존 RPH1 `BOOT_MODULES` section을
생성한다. `initial-image`는 entry flag bit 0, `auxiliary`는 0으로 투영된다. Reserved와
name-digest field는 RPH1 v1.0 계약대로 0이다. Parser와 producer는 count, entry size, flags,
singleton, range와 overlap을 독립적으로 재검사한다.

다른 Boot Protocol은 같은 `RibonBootEnvironment.boot_modules`를 자기 wire 계약으로
투영하거나 지원하지 않는 경우 transaction을 거부한다. raw-FDT loader에 Parus, Linux 또는
FreeBSD 분기를 추가하지 않는다.

## 실패와 evidence 경계

Manifest, product authority, generated service, linker layout, materialization, environment,
protocol expectation 중 하나라도 실패하면 payload transfer 전 architecture halt로 전환한다.
부분 module inventory를 publication하지 않는다.

Host/unit은 malformed input과 capacity를 증명한다. QEMU는 raw-FDT producer와 consumer의
guest-executed 경로를 증명한다. RPi5 package v2는 embedded offset, alignment, exact byte/hash와
provenance 결합을 증명하지만 physical RPi5 실행, production secure boot 또는 update authority를
증명하지 않는다.
