---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-08-02
code_paths:
  - tools/qemu_target_smoke.py
  - tools/validate_external_parus_payload.py
  - tools/generate_boot_module_bundle.py
  - tools/prepare_external_linux_image.py
  - tools/prepare_external_freebsd.py
  - tools/compose_freebsd_uefi.py
  - tools/build_linux_initramfs.py
  - products/bootmgr/manifests/qemu-aarch64-virt-linux.json
  - products/bootmgr/manifests/qemu-aarch64-virt-parus-modules.json
  - products/bootmgr/manifests/qemu-aarch64-virt-modules-fixture.json
  - products/bootmgr/manifests/qemu-aarch64-virt-parus-external.json
  - products/bootmgr/manifests/qemu-riscv64-virt-rph1-fixture.json
  - products/bootmgr/manifests/x86_64-uefi-parus-fixture.json
  - products/bootmgr/manifests/x86_64-uefi-parus-external.json
  - products/bootmgr/manifests/x86_64-uefi-linux.json
  - products/bootmgr/manifests/x86_64-uefi-freebsd.json
  - tests/fixtures/riscv64/
  - tests/tools/qemu_target_smoke_tests.py
  - tests/tools/external_parus_payload_tests.py
  - tests/tools/external_linux_image_tests.py
  - tests/tools/external_freebsd_tests.py
tests:
  - make check-qemu-evidence
  - make check-boot-modules
  - make check-uefi-product-hermeticity
  - make qemu-aarch64-virt-linux-smoke
  - make qemu-aarch64-virt-modules-fixture-smoke
  - make QEMU_PARUS_PAYLOAD=/path/to/parus.elf QEMU_PARUS_MODULE_COMPONENT_MANIFEST=/path/to/components.json qemu-aarch64-virt-parus-modules-smoke
  - make x86_64-uefi-parus-fixture-smoke
  - make UEFI_PARUS_PAYLOAD=/path/to/parus.elf x86_64-uefi-parus-external-smoke
  - make x86_64-uefi-linux-smoke
  - make x86_64-uefi-freebsd-smoke
  - make QEMU_PARUS_PAYLOAD=/path/to/parus.elf qemu-aarch64-virt-parus-smoke
  - make qemu-riscv64-virt-rph1-fixture-smoke
hardware:
  - none
supersedes:
  - none
---

# QEMU payload evidence 계약

Ribon의 QEMU harness는 generated fixture와 외부 kernel payload를 서로 다른 product
class로 검증한다. Fixture marker를 포함한 ELF는 external-kernel result를 만들 수 없고,
실행 전후 payload SHA-256이 다르면 해당 실행은 artifact identity failure다.

## 입력 authority

Harness 입력은 target, expected payload class, payload path, source revision, composed
artifact와 선택적 firmware다. Payload bytes에서 ELF identity와 fixture marker를
검사해 observed payload class를 별도로 기록한다. Expected class 선언은 observed
class를 덮어쓰지 않는다.

AArch64 actual product는 `arm64-rph1-v1`, AArch64 ELF64, load window
`[0x41000000, 0x42000000)`를 manifest로 고정한다. Product build 전 검증은 모든
`PT_LOAD`의 file/memory bound, 비중첩, W^X, executable entry와 실행 중 payload
immutability를 확인한다. Fixture target은 별도 build directory와 generated fixture를
계속 사용한다.

External-kernel class는 actual payload identity와 Ribon transfer 증거를 연다. Parus
boot stage 또는 runtime 성공은 별도 required marker graph와 Parus integration
harness가 검증해야 하며, payload class만으로 열리지 않는다.

## AArch64 Linux raw Image 증거

Linux product는 ELF external-kernel class를 재사용하지 않는다. Source descriptor는 upstream
release URL, distribution/release/target identity, license notice, exact size, maximum size와 SHA-256를
고정한다. Build는 cache가 존재해도 digest와 AArch64 Linux Image header를 다시 검증한다. Raw Image는
Ribon binary에 포함하지 않고 product placement 주소에 QEMU loader로 별도 pre-load한다. Generated
descriptor object는 같은 주소와 exact file size만 Core의 generic memory boot source에 게시한다.

Initramfs는 source-owned AArch64 assembly PID 1을 static ELF로 만든 뒤 deterministic `newc` archive로
직렬화한다. Raw-FDT module bundle은 `initramfs` auxiliary module과 BOOT_MODULE reservation을 제공한다.
Linux protocol만 `/chosen/linux,initrd-start`와 `linux,initrd-end`를 compact copied FDT에 추가한다.
Generic raw-FDT frontend, placement service와 module bundle에는 Linux branch가 없다.

Success는 Ribon lifecycle marker, Linux PID 1 marker가 정확히 한 번, Linux power-down receipt가 정확히
한 번 나타나고 QEMU가 자체적으로 status 0으로 끝날 때만 성립한다. Harness는 이 product에서
`-no-shutdown`을 사용하지 않으며 forced termination을 success로 바꾸지 않는다. Result는 source
descriptor, external validation, product, raw Image, initramfs provenance, composed Ribon image, QEMU
command/version, serial hash와 clean exit code를 분리해 보존한다.

## raw-FDT typed module 증거

raw-FDT module evidence는 generated provenance만으로 열리지 않는다. Harness는 실행 전에
다음 authority와 byte identity를 모두 검증한다.

- 선택한 product manifest가 정확한 raw-FDT target tuple과 `bootloader` product kind를 가진다.
- product가 `BOOT_MODULE_BUNDLE`을 required/allowed capability로 모두 선언한다.
- product가 canonical `service.product.boot-module-bundle` provider를 정확히 하나 선택한다.
- provenance의 product ID와 product manifest SHA-256이 선택한 product와 일치한다.
- component는 1..8개이고, 이름·role·순서·크기·SHA-256·snapshot이 exact하다.
- initial-image role은 최대 하나이며 bundle digest를 snapshot bytes에서 다시 계산한다.
- ordinal snapshot bytes와 page-aligned backing 크기가 composed raw image의 canonical suffix와
  정확히 일치한다. 동일 byte 내용을 가진 서로 다른 module도 ordinal로 구분한다.

위 결합 중 하나라도 실패하면 QEMU를 launch하지 않고 preflight failure를 기록한다. 실행한
경우 payload, product manifest, module provenance와 snapshot, composed image의 실행 전후
SHA-256 및 재검증 결과를 모두 기록한다. 하나라도 변경되면 성공 marker가 관측되었더라도
artifact identity failure다.

## x86_64 UEFI product 격리

`bootmgr.x86_64-uefi-parus-fixture`, `bootmgr.x86_64-uefi-parus-external`,
`bootmgr.x86_64-uefi-linux`와 `bootmgr.x86_64-uefi-freebsd`는 서로 다른 product root,
registry, object, link map,
ESP와 result를 사용한다. Fixture→external→fixture와 그 역순의 증분 빌드는 이미 생성된
반대 product output을 변경해서는 안 된다. 동일 input을 독립 build root에서 조합한
canonical application과 ESP artifact는 byte-identical이어야 한다.

Hermeticity gate가 synthetic external-input ELF를 사용하는 경우 그 실행은 build dependency와
output isolation의 host evidence일 뿐 external Parus runtime evidence가 아니다. External QEMU
evidence는 별도의 실제 kernel payload, external product manifest와 Parus terminal marker graph를
모두 요구한다.

UEFI QEMU 실행은 ESP directory를 read-only VVFAT base로 열고 QEMU의 transient block snapshot에
firmware NvVars write를 격리한다. 따라서 실행에 필요한 firmware write는 허용하되 선택한 ESP
input의 byte identity는 실행 전후 동일해야 한다.

## x86_64 Linux EFI-stub 증거

Linux product는 OpenWrt 24.10.0 x86/64 kernel의 exact size `5739520`과 SHA-256
`2a0deaeab7dd3edf23c68597e1c79e0bd0f1ad92381cc90b3abd0187e96f28fe`를 고정한다. Build validator는
PE32+, x86_64 machine과 EFI application subsystem을 확인한다. Ribon은 source를 PE/COFF parser로
검증한 뒤 generic terminal launcher가 OVMF `LoadImage()`/`StartImage()`로 exact child를 실행한다.

Success는 managed transaction launch marker, Linux PID 1 marker
`RIBON:LINUX:X86_64:PID1:v1:OK`, `reboot: Power down`과 QEMU status 0을 요구한다. Result는 external
validation, kernel, initramfs, product manifest, ESP, firmware와 raw serial hash를 분리해 보존한다.
이 증거는 runtime network, physical hardware, Secure Boot나 모든 EFI-stub kernel 지원을 뜻하지 않는다.

## x86_64 FreeBSD official-loader 증거

FreeBSD product는 15.1-RELEASE amd64 mini-memstick compressed/raw size와 SHA-256, 공식 signed checksum
document identity를 고정한다. Build는 official raw cache를 변경하지 않고 별도 copy의 FAT32 ESP에
Ribon application, `/RIBON/BOOT.CFG`와 exact official loader를 조합한다. Package provenance는 official
source immutability, loader hash와 composed disk hash를 QEMU preflight에 결합한다.

Harness는 official loader marker를 `freebsd-efi` observed class로 독립 분류하며 expected class가 이를
덮어쓰지 못한다. Success는 ordered Ribon managed-launch marker, loader revision banner, FreeBSD
15.1-RELEASE kernel banner와 single-user pathname prompt를 요구한다. 이 terminal evidence가 관측되면
QEMU process group을 bounded cleanup하며 forced kill은 허용하지 않는다. 이는 clean guest shutdown,
login, installer, runtime network, physical hardware 또는 PGP signature verification 증거가 아니다.

## RISC-V RPH1 contract fixture

`bootmgr.qemu-riscv64-virt-rph1-fixture`는 external-kernel product와 다른 manifest,
build directory와 marker graph를 사용한다. Fixture ELF는 Ribon tree가 소유하며 다음
경계를 독립 소비한다.

- OpenSBI `fw_dynamic`의 S-mode entry와 bootstrap hart 0
- Ribon raw-FDT lifecycle과 payload placement
- `a0=RPH1`, `a1=RPH1 flag` register ABI
- terminal entry의 `satp=0`과 masked `sstatus.SIE`
- RPH1 magic, version, bounded table와 CRC32C
- RISC-V provenance와 required singleton `BOOT_CPU`

Harness는 fixture provenance를 external kernel과 구분한다. Success marker는
`RIBON-RPH1-RISCV64-FIXTURE-OK`이고 `PARUS:*` runtime marker를 요구하지 않는다.
`RIBON-RPH1-RISCV64-FIXTURE-FAIL:`은 timeout을 기다리지 않는 terminal fixture
failure다.

## Result와 cleanup

Result는 다음 authority를 분리해 보존한다.

- expected product class와 observed payload class
- Ribon source revision
- payload와 composed artifact SHA-256
- 선택한 product manifest의 ID와 SHA-256
- module provenance, component snapshot, bundle digest와 raw-image suffix binding
- firmware SHA-256
- QEMU version과 실제 command
- bounded timeout과 terminal reason
- process-group cleanup, forced kill, stale process group
- raw serial path/hash
- required marker count/order와 first divergence

Preflight rejection처럼 QEMU를 launch하지 않은 결과도 `cleanup` record를 가진다.
성공은 required marker가 정확히 한 번 순서대로 관측되고, 모든 선택 artifact가 immutable하며,
cleanup complete와 forced kill false일 때만 성립한다. Required marker 관측 뒤 process cleanup에서
추가로 읽은 serial tail도 다시 분류하며, panic, unhandled exception, target failure 또는 fixture
failure가 있으면 앞선 성공을 취소한다.

## Claim 경계

Ribon QEMU evidence success는 선택된 QEMU target과 payload identity에 한정된다.
Physical hardware, production firmware, Parus full boot, VM, SMP 또는 user runtime
성공을 대신하지 않는다.
