# Ribon

Ribon은 OS와 architecture에 독립적인 deterministic boot runtime이다. 같은 기반을
bootloader executable, embeddable library, firmware/plugin SDK product로 조합한다.

공개 기능은 다음 경계로 분리된다.

- `libribon-core`: caller-owned arena, context, plugin descriptor와 immutable registry
- `libribon-boot`: firmware-neutral service, environment, image plan과 boot lifecycle
- Architecture plugin: payload validation, cache, privilege와 terminal entry
- Image-format plugin: ELF, PE/COFF 등의 bounded parser와 load plan
- Boot Protocol plugin: OS component, handoff wire ABI, register ABI와 confirmation
- Environment plugin: UEFI, BIOS, raw-FDT, SBI, host service의 consumer
- Firmware Personality plugin: UEFI/BIOS-compatible service의 provider

Plugin은 runtime scan이나 constructor로 등록하지 않는다. QStar product manifest가
plugin set을 선택하고 `build/` 아래 immutable registry와 product descriptor를
생성한다. `RibonProfile`, `RibonFirmwareAdapter`, builtin registry를 위한 compatibility
API는 제공하지 않는다.

Parus는 generic Core의 특수 분기가 아니라 `protocol.parus` Boot Protocol plugin이다.
Parus Handoff v1(`RPH1`) serializer와 parser는
`src/protocols/parus/`와 `include/Ribon/protocols/parus/`가 소유한다.

## Build와 검증

```sh
make lib
make host-reference
make check
make check-target-builds
make qemu-aarch64-virt-raw-fdt-smoke
make x86_64-uefi-app-smoke
make qstar-check
make docs
```

`make check`는 public API layout, legacy ABI hard cut, plugin graph negative tests,
protocol-free embed, host-reference plan, architecture matrix와 object graph를 검사한다.
QEMU smoke는 x86_64 UEFI consumer와 AArch64 raw-FDT target의 runtime evidence다.
BIOS는 compile-only, RPi5는 package evidence이며 physical board boot 증거가 아니다.

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
