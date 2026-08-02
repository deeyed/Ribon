---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-08-02
code_paths:
  - include/Ribon/protocols/os/
  - src/protocols/os/
  - products/bootmgr/
tests:
  - make check-os-packages
  - make check-linux-boot
  - make qemu-aarch64-virt-linux-smoke
  - make qemu-aarch64-virt-parus-smoke
  - make qemu-riscv64-virt-parus-smoke
  - make x86_64-uefi-parus-external-smoke
  - make x86_64-uefi-linux-smoke
hardware:
  - RPi5 evidence is independent and requires fresh UART
supersedes:
  - protocol package presence as runtime support
---

# OS protocol package 지원 행렬

OS 이름의 디렉터리와 descriptor가 존재하는 사실은 runtime support가 아니다. 각
package는 공식 wire/entry 계약을 독립적으로 소유하고, 아래 acceptance를 통과한
tuple만 실행 지원을 주장한다.

| OS package | 구현 경계 | 허용 주장 | 열지 않는 주장 |
| --- | --- | --- | --- |
| Parus | RPH1 build/parse, AArch64·x86_64·RISC-V invocation | product별 QEMU 또는 hardware evidence가 있는 tuple | 모든 board, production firmware, feature parity |
| Linux | AArch64 raw `Image` direct entry와 x86_64 EFI-stub managed image | pinned OpenWrt AArch64·x86_64 image의 product별 QEMU PID 1 boot | RISC-V Linux, bzImage direct loader, physical board와 production firmware |
| FreeBSD | PE/COFF validation, managed terminal requirement와 UEFI provider binding | descriptor, validation, terminal contract only | 실제 `loader.efi`와 kernel runtime boot |
| Zircon | bounded ZBI container validation과 AArch64 invocation | unit-level experimental protocol contract | complete ZBI item policy 또는 Zircon runtime boot |
| Windows | 없음 | unsupported | placeholder, PE/COFF parser만으로 boot 가능하다는 주장 |
| macOS | 없음 | unsupported | Apple firmware, licensing 또는 hardware compatibility |

## Runtime acceptance

실행 지원은 다음을 모두 요구한다.

1. 해당 OS의 공식 image 형식과 wire ABI를 bounded parser로 검증한다.
2. product manifest가 architecture, environment, image-format과 protocol stable ID를
   명시적으로 선택한다.
3. generic frontend가 selected protocol/image descriptor만 사용하고 OS symbol을 직접
   참조하지 않는다.
4. supervised execution이 source revision, payload hash, raw log, marker ordering,
   timeout과 process-group cleanup을 보존한다.
5. physical target 주장은 fresh hardware capture를 별도로 요구한다.

FreeBSD UEFI protocol은 `FIRMWARE_MANAGED_IMAGE`를 선택하고 handoff 또는 register invocation을
생성하지 않는다. UEFI product는 generic launcher를 선택할 수 있지만 실제 FreeBSD artifact, loader
configuration과 kernel을 사용한 runtime evidence는 아직 없다. Provider 존재만으로 FreeBSD runtime
support가 되지 않는다.

현재 Linux 실행 증거는 AArch64 QEMU virt raw-FDT와 x86_64 QEMU q35 OVMF에서 각각 Ribon
lifecycle, pinned image, `/init` unique marker와 clean poweroff까지 도달한다. x86_64 경로는
firmware-managed EFI stub이며 direct PE loader가 아니다. 각 증거는 descriptor가 고정한 OpenWrt
release와 hash 및 해당 QEMU tuple에만 한정된다. Parus 실행 증거는 AArch64
QEMU virt와 x86_64 QEMU q35가 full Parus IDLE receipt까지 도달한다. RISC-V QEMU virt는
OpenSBI→Ribon→Parus transfer와 EB2까지의 evidence만
있고 EB3 Sv39 authority에서 fail-closed한다. 이 결과는 RISC-V runtime boot support로
승격되지 않는다.

## Boot health confirmation 지원

Generic {doc}`../update/boot-confirmation-v1`은 OS-independent envelope, attempt freshness, signature와
journal commit을 제공한다. 그러나 각 OS package의 health payload는 companion producer와 protocol
codec이 함께 있어야 한다. 현재 Parus, Linux, FreeBSD와 Zircon callback은 모두 fail-closed
`UNSUPPORTED`이며, D06 QEMU evidence는 validation protocol receipt만 사용한다. 따라서 generic
confirmation 구현을 각 OS의 실제 health confirmation 지원으로 해석하지 않는다.
