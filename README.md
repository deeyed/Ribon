# Ribon

Ribon은 OS와 architecture에 독립적인 deterministic boot runtime이다. 같은 기반을
bootloader executable, embeddable library, firmware/plugin SDK product로 조합한다.

공개 기능은 다음 경계로 분리된다.

- `libribon-core`: caller-owned arena, context, plugin descriptor와 immutable registry
- `libribon-boot`: firmware-neutral service, environment, image plan과 boot lifecycle
- `libribon-sdk`: external package ABI, host contract harness와 firmware service publication
- Architecture plugin: payload validation, cache, privilege와 terminal entry
- Image-format plugin: ELF, PE/COFF 등의 bounded parser와 load plan
- Boot Protocol plugin: OS component, handoff wire ABI, register ABI와 confirmation
- Environment plugin: UEFI, BIOS, raw-FDT, SBI, host service의 consumer
- Port service: machine wiring을 diagnostic, machine-description, payload-placement
  authority로 분리
- Firmware Personality plugin: UEFI/BIOS-compatible service의 provider

Plugin은 runtime scan이나 constructor로 등록하지 않는다. QStar product manifest가
plugin set을 선택하고 `build/` 아래 immutable registry와 product descriptor를
생성한다. `RibonProfile`, `RibonFirmwareAdapter`, builtin registry를 위한 compatibility
API는 제공하지 않는다.

Parus는 generic Core의 특수 분기가 아니라 `protocol.parus` Boot Protocol plugin이다.
Parus Handoff v1(`RPH1`) serializer와 parser는
`src/protocols/os/parus/`와 `include/Ribon/protocols/os/parus/`가 소유한다.
Linux, FreeBSD, Zircon은 같은 generic frontend를 소비하는 독립 OS protocol package다.
Package 존재와 실제 runtime support는
[`docs/contracts/protocols/os-package-support-matrix.md`](docs/contracts/protocols/os-package-support-matrix.md)
기준으로 구분한다.

## Build와 검증

GNU Make가 공개 build, test, package, QEMU와 documentation frontend다. QStar는
product/plugin composition graph 검증기로 유지되며 `make qstar-check`와 aggregate
`make check`에서 실행된다. 루트 Makefile은 `make/`의 기능별 모듈만 include한다.

필수 host 도구는 C compiler, Python 3, LLVM `clang`/`lld`/`llvm-objcopy`/`llvm-ar`,
QStar다. QEMU runtime에는 `qemu-system-aarch64`, `qemu-system-x86_64`,
`qemu-system-riscv64`, OVMF와 OpenSBI가 필요하고, 문서에는 Doxygen과
`docs/requirements.txt`의 Python package가 필요하다. 설치 위치는 고정하지 않는다.
PATH 탐지를 쓰며 모든 도구와 firmware는 Make 변수로 덮어쓸 수 있다.

```sh
make help
make doctor
make lib
make sdk-install
make check-sdk-surface
make check-external-plugin
make check-firmware-personalities
make host-reference
make check-ribos-parser-pilot
make check-ribos-semantics
make check-ribos-schema
make check-ribos-ir
make check-ribos-resources
make check-ribos-artifact
make check-ribos-executable-corpus
make check
make check-target-builds
make qemu-aarch64-virt-raw-fdt-smoke
make qemu-riscv64-virt-rph1-fixture-smoke
make x86_64-uefi-parus-fixture-smoke
make check-uefi-product-hermeticity
make qstar-check
make docs
```

일반적인 override 예시는 다음과 같다.

```sh
make CROSS_CC=clang-18 LD_LLD=ld.lld-18 check-target-builds
make X86_64_UEFI_FIRMWARE=/path/to/OVMF_CODE.fd \
     RISCV64_OPENSBI_FIRMWARE=/path/to/fw_dynamic.bin ci-qemu
make BUILD_ROOT=/tmp/ribon-build check
```

GitHub Actions는 Ubuntu에서 host aggregate, target build, 세 architecture QEMU
fixture와 documentation lane을 각각 실행한다. QEMU 성공은 physical RPi5 evidence가
아니다.

Ribos language project는 `language/ribos/` 아래에서 `frontend`, versioned product
`schema`, VM 독립 Policy `ir`, canonical bytecode `artifact`와 향후 `vm` 계층을
분리한다. 공식 source 확장자는
`.rbs`이며 legacy alias는 없다. Pegen action은 bounded Ribos AST를 생성하고 semantic
gate는 type, mutation, match, capability와 helper-call upper bound를 검사한다. Policy
IR gate는 typed virtual slot, explicit CFG, direct call/branch, aggregate shape,
source map, helper call-site와 product schema identity를 검사한다. Resource gate는
reachable CFG, bounded loop, terminal closure, frame/stack layout, instruction과
helper별 upper bound를 검사하고 declared budget을 집행한다. Artifact gate는 VM ABI
1.0/ISA 1.0의 little-endian table, payload SHA-256, signature envelope shape,
overflow-safe range와 deterministic corpus를 검사한다. Dict는 fixed-capacity sorted
array와 bounded linear search를 사용한다. 일반 build는
`language/ribos/frontend/generated/`의 추적되는 C parser snapshot을 직접 컴파일하며
`make check`도 Pegen을 실행하지 않는다. 문법이나 generator integration을 바꿀 때만
다음 explicit gate를 사용한다.

Public Ribos example은 `language/ribos/examples/executable/`에 있으며 parser부터
independent verifier와 production target-core VM의 host terminal outcome까지 실행된다.
Sphinx 문서의 tagged example block, source, artifact digest와 exact resource closure는
versioned manifest로 함께 고정된다. Parser-only syntax 조각은
`language/ribos/frontend/tests/fragments/`로 분리되어 실행 가능성의 증거로 사용되지 않는다.

```sh
make ribos-parser-generate RIBOS_PEGEN_ROOT=/path/to/cpython/Tools/peg_generator
make ribos-parser-regenerate-check RIBOS_PEGEN_ROOT=/path/to/cpython/Tools/peg_generator
```

`make check`는 public API layout, legacy ABI hard cut, plugin graph negative tests,
protocol-free embed, Ribos parser·semantic·schema·Policy IR·resource-closure·artifact
corpus, 여섯 target archive의 SDK install reproducibility, installed-only typed Ribos
extension, external plugin package, firmware reference provider, host-reference plan,
architecture matrix와 object graph를 검사한다.
QEMU smoke는 x86_64 UEFI consumer와 AArch64 raw-FDT target의 runtime evidence다.
RISC-V RPH1 fixture smoke는 OpenSBI, Ribon lifecycle, RPH1 `BOOT_CPU`와
S-mode/MMU-off entry를 잇는 Ribon 소유 계약 증거이며 실제 Parus kernel boot가 아니다.
BIOS consumer와 UEFI/BIOS-compatible provider는 compile-only, RPi5는 package evidence다.
Reference provider 성공은 bootable firmware나 specification conformance 증거가 아니며
physical board boot 증거도 아니다.

## 문서

Ribon 문서는 Sphinx + MyST Markdown + Breathe 위계를 사용한다.

- `docs/canonical/`: 장기 설계와 ownership
- `docs/contracts/`: ABI, lifecycle와 composition obligation
- `docs/adr/`: 결정과 supersession history
- `docs/platforms/`: target과 board-specific boundary
- `docs/roadmap/`: capability 의존 순서
- `docs/log/`: 실행 시점 상태와 evidence

최종 구조는
[`docs/canonical/architecture/ribon-architecture.md`](docs/canonical/architecture/ribon-architecture.md),
문서·주석 규칙은
[`docs/policy/documentation-policy.md`](docs/policy/documentation-policy.md)를 따른다.
