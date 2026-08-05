---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-08-05
code_paths:
  - Makefile
  - make/
  - tools/make/doctor.py
  - tools/make/find_firmware.py
  - tools/make/find_llvm_tool.py
  - qstar.lua
  - qstar/
  - .github/workflows/ci.yml
tests:
  - make check-build-system
  - make doctor
  - make check
  - make check-target-builds
  - make ci-qemu
  - make docs
hardware:
  - none
supersedes:
  - monolithic host-path-bound Makefile
---

# 휴대형 Make 실행 계약

## 권위

GNU Make는 source checkout의 build, host/unit test, cross-target build, package, QEMU,
SDK install과 documentation을 실행하는 공개 frontend다. QStar는 제거하지 않으며
source-owned product manifest, plugin closure와 object graph를 독립적으로 검증한다.

Make와 QStar는 서로 다른 product, capability, service, security 또는 target 의미를
정의할 수 없다. Product tuple과 trust identity는
{doc}`../composition/product-plugin-composition`의 manifest가 소유한다. Make recipe는
해당 manifest에서 registry와 artifact를 생성하고 QStar는 같은 입력의 closure를
검사한다.

## 모듈 경계

루트 `Makefile`은 project root와 include 순서만 소유한다. 구현은 다음 모듈로
분리한다.

| 모듈 | 소유권 |
| --- | --- |
| `make/config.mk` | caller override, tool name, architecture flag와 build root |
| `make/model.mk` | source list, product path와 generated artifact identity |
| `make/rules/tooling.mk` | dependency doctor, self-test와 CI aggregate |
| `make/rules/core.mk` | library, host reference와 공통 object rule |
| `make/rules/ribos.mk` | Ribos compiler, verifier, VM과 cross-architecture evidence |
| `make/rules/security-update.mk` | signature, key policy, update와 recovery |
| `make/rules/raw-fdt.mk` | raw-FDT AArch64/RISC-V와 RPi5 package |
| `make/rules/uefi-bios.mk` | UEFI, EFI payload, FreeBSD와 BIOS |
| `make/rules/host-sdk.mk` | public API, SDK, firmware provider와 object graph |
| `make/rules/aggregate.mk` | aggregate check, docs와 clean |

모든 tracked module은 `RIBON_MAKEFILES` prerequisite에 포함한다. Flag, source list,
link recipe 또는 dependency가 바뀌면 이전 object가 무효화되어야 한다.

## 도구 탐지

Make graph에는 사용자 home, package manager Cellar version 또는 한 host의 executable
절대 경로를 기록하지 않는다. 기본값은 PATH의 명령 이름이다.

다음 변수는 caller가 command line 또는 environment에서 덮어쓸 수 있다.

```text
CC AR PYTHON QSTAR OPENSSL
CROSS_CC X86_64_CC AARCH64_CC RISCV64_CC
LD_LLD LLD_LINK OBJCOPY LLVM_AR
QEMU_AARCH64 QEMU_X86_64 QEMU_RISCV64
DOXYGEN SPHINX_BUILD
X86_64_UEFI_FIRMWARE RISCV64_OPENSBI_FIRMWARE
```

Firmware resolver는 선택한 QEMU executable의 install prefix와 FHS data root에서
OVMF/OpenSBI를 찾는다. 발견 결과가 없으면 임의 파일을 선택하지 않는다. QEMU lane은
`doctor-qemu`에서 missing firmware를 fail-closed로 보고한다.

## 공개 target

- `make all`, `make lib`, `make host-reference`: host build
- `make check`: host/unit, security/update cross compile, object graph와 QStar aggregate
- `make check-target-builds`: BIOS, RPi5 package, raw-FDT와 UEFI product build
- `make ci-qemu`: AArch64, RISC-V와 x86_64 fixture runtime
- `make sdk-install`: public SDK installation
- `make docs`: lint, Doxygen XML과 warnings-as-errors Sphinx
- `make doctor`: 모든 lane의 tool/firmware dependency

`BUILD_ROOT`를 바꾸면 object, generated registry, package, result, Doxygen과 Sphinx
output이 모두 그 root 아래에 있어야 한다.

## CI와 증거 한계

Linux CI는 host aggregate, target build, QEMU fixture와 documentation을 분리 실행한다.
CI의 target build는 compile/package evidence이며 QEMU fixture는 명시된 machine의
runtime evidence다. 어느 lane도 physical RPi5, production secure boot 또는 외부 OS
전체 호환성을 증명하지 않는다.
