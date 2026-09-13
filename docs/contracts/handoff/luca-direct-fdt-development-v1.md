---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-09-13
code_paths:
  - include/Ribon/protocols/os/luca/direct_fdt.h
  - products/bootmgr/manifests/aarch64-uefi-luca-direct-fdt-dev.json
  - src/protocols/os/luca/direct_fdt.c
  - src/protocols/os/luca/direct_protocol.c
  - src/environments/uefi-app/uefi_app.c
  - src/arch/aarch64/arch.c
  - ../../../../sys/boot/input/direct_fdt/importer.c
  - ../../../../sys/kern/boot/dtb.c
tests:
  - make check-luca-direct-fdt
  - make check-arch-ops
  - make check-uefi-exit-transaction
  - make check-aarch64-uefi-luca-direct-fdt-negative-smoke
  - make aarch64-uefi-luca-direct-fdt-dev-smoke
hardware:
  - qemu-virt-aarch64-edk2
supersedes:
  - none
---

# LUCA direct-FDT development handoff v1 계약

이 계약은 AArch64 UEFI application인 Ribon이 LUCA kernel과 World를 적재한 뒤 선택된
development direct-FDT 경로로 제어권을 넘기는 producer/consumer 경계를 정의한다.
제품 ID는 `bootmgr.aarch64-uefi-luca-direct-fdt-dev`, protocol ID는
`luca-direct-fdt-dev`, plugin ID는 `protocol.luca-direct-fdt-dev`다. Manifest, 생성된
registry와 `BOOT.CFG`가 이 ID를 명시해야 하며 runtime에서 다른 protocol의 실패를 받아
이 경로로 전환하지 않는다.

이 형식은 RLH1의 다른 버전이나 호환 wire가 아니다. RLH1 signature, protected floor와
rollback 검증 실패는 그대로 fail-closed다. Direct-FDT의 payload hash와 exact span
비교는 개발 이미지의 identity와 손상 관찰이며 authenticity 또는 rollback resistance를
제공하지 않는다.

## 선택 제품과 입력

선택 제품은 UEFI, AArch64, `qemu-virt-aarch64`, normal mode와 ELF64 image provider를
고정한다. ESP의 canonical 입력은 다음 세 파일이다.

| 경로 | 역할 | 필수성 |
| --- | --- | --- |
| `/RIBON/BOOT.CFG` | exact product와 protocol 선택 | 필수 |
| `/RIBON/LUCA.ELF` | LUCA kernel ELF64 | 필수 |
| `/RIBON/WORLD.PKG` | initial-image 역할의 World package | 필수 |

Protocol component closure는 kernel 하나와 boot-module 하나만 받는다. Kernel은 ELF
program header의 file/memory bounds, overflow, machine과 executable entry coverage를
검증한 뒤 exact physical segment에 배치한다. World는
`RIBON_BOOT_MODULE_ROLE_INITIAL_IMAGE` 하나여야 한다. Missing World, 빈 span, 두 World,
지원하지 않는 role이나 kernel과 겹치는 span은 effect를 성공으로 바꾸지 않는다.

QEMU virt development runtime RAM 창은 half-open
`[0x40000000, 0x80000000)`이다. Kernel의 모든 배치 segment, entry, World와 생성되는
handoff buffer가 이 창 안에 완전히 있어야 한다. Address와 size 덧셈은 overflow를
검사하고 kernel/World/handoff storage 사이 overlap을 거부한다. 이 창은 selected virtual
product의 배치 계약이며 일반 LUCA PMM 한도나 physical board memory map이 아니다.

## FDT wire projection

Ribon은 UEFI configuration table에서 획득한 source FDT를 bounds가 검증된 borrowed input으로
사용한다. Source와 destination은 겹칠 수 없다. FDT magic, total size, structure/string/
reservation block bounds, version, token depth, root `#address-cells`와 정확히 한 `/chosen`
node를 검증한다. Address cell 수는 1 또는 2만 허용한다. Ribon이 소유하는 property가
source `/chosen`에 이미 있으면 duplicate authority이므로 거부한다.

Ribon은 `/chosen`에 다음 big-endian scalar를 함께 기록한다.

| Property | Producer 값 | LUCA consumer 투영 |
| --- | --- | --- |
| `luca,kernel-physical-start` | 모든 runtime kernel segment를 감싸는 최소 physical address | `BootDtbKernelImage.physical.address` |
| `luca,kernel-physical-end` | 같은 span의 exclusive end | `BootDtbKernelImage.physical.size` 계산의 end |
| `luca,kernel-entry` | executable segment 안의 runtime entry | `BootDtbKernelImage.entry` |
| `linux,initrd-start` | World physical address | initial-image `BootPhysicalSpan.address` |
| `linux,initrd-end` | World exclusive end | initial-image `BootPhysicalSpan.size` 계산의 end |

Kernel tuple의 세 property와 World tuple의 두 property는 각각 all-or-nothing이다. Scalar
폭은 root address-cell 폭과 일치해야 한다. Zero length, end 역전, 중복 property, entry가
kernel span 밖인 경우, 32-bit cell로 표현할 수 없는 64-bit 값은 malformed input이다.
LUCA는 kernel tuple을 selected linker physical span과 다시 비교하고 World를 external
initial-image artifact로 등록한다. Property 이름은 kernel pointer, virtual mapping,
signature 결과나 실행 권한을 운반하지 않는다.

## Reservation과 수명

Ribon은 source FDT reservation map과 다음 half-open range를 address 순서로 병합하여 새
FDT reservation map에 기록한다.

- runtime kernel span 전체
- exact World span
- transfer까지 유지되는 generated FDT buffer capacity
- final UEFI memory map에서 usable, MMIO, framebuffer 또는 boot-reclaimable로 분류되지
  않은 firmware range

Merged reservation은 최대 64개다. Range overflow, destination capacity 부족, terminator
부재 또는 malformed source reservation은 실패다. FDT buffer, kernel pages와 World pages는
`ExitBootServices()` 뒤 LUCA가 EB2 protected residency로 수용할 때까지 해제하거나
재사용하지 않는다. LUCA importer는 generated FDT 자체, kernel과 World를 별도 residency
kind로 봉인하고 generic firmware reservation에서 kernel span을 잘라 중복 owner를 만들지
않는다. Firmware가 usable이라고 보고한 사실만으로 payload나 handoff backing을 PMM에
즉시 돌려주지 않는다.

## Final memory map과 firmware 종료

Target은 boot media, World list와 command-line 같은 semantic input을 caller-owned persistent
view로 동결한다. `ribon_uefi_app_exit_boot_services()`는 최대 3회 다음 transaction을
반복한다.

1. `GetMemoryMap()`으로 raw descriptors와 새 map key를 capture한다.
2. Native pointer가 없는 normalized memory regions를 다시 만든다.
3. Persistent semantic input을 다시 적용한다.
4. 새 final map으로 FDT handoff와 reservation을 다시 생성한다.
5. 그 capture가 반환한 exact key로 `ExitBootServices()`를 호출한다.

`EFI_INVALID_PARAMETER`만 changed-key retry를 허용한다. 다른 firmware error, capture/
capacity 오류, persistent-input 불일치 또는 handoff refresh 실패는 즉시 닫힌 실패다.
세 번째 changed-key도 실패하면 `RIBON_UEFI_APP_STATUS_RETRY_EXHAUSTED`다. 성공 순간
context의 Boot Services pointer를 지우며 이후 environment quiesce는 이 closure를 확인한다.
Firmware allocator, filesystem, console, input과 memory-map service는 성공 뒤 다시 호출하지
않는다.

## AArch64 entry state

Prepared entry는 다음 exact contract를 가진다.

| 항목 | 값 |
| --- | --- |
| entry address | validated runtime kernel entry |
| `x0` | generated FDT physical pointer |
| `x1`–`x3` | 0 |
| argument count | 1 |
| interrupts | masked |
| privilege | AArch64 EL1 |
| translation | disabled |

AArch64 validator는 EL1 privilege와 disabled translation을 한 쌍으로만 허용한다. 다른
architecture, register ABI, unmasked interrupt, unsupported enum 또는 privilege/translation
혼합은 transfer 전에 거부한다. Architecture transfer는 current EL이 EL1인지 확인하고,
data/instruction visibility barrier 뒤 `SCTLR_EL1.M/C/I`를 내리고 TLB를 무효화한 다음
`x0`에 FDT pointer를 놓아 branch한다. Ribon은 LUCA permanent page table이나 exception
runtime을 소유하지 않는다.

## Failure evidence와 claim 경계

Malformed config, missing kernel/World, corrupt ELF와 handoff preparation 실패는
`RIBON-R4-UEFI-TRANSFER` 전에 닫혀야 한다. Corrupt kernel과 missing World는 selected
direct-FDT application을 실제 EDK2/QEMU에서 부팅하여 각각 `transaction-prepare-image`와
`init-image-load` failure stage로 확인한다. Overlap, malformed FDT, duplicate property,
wrong entry state와 changed map key는 owner unit tests에서 exact rejection 또는 bounded
retry를 확인한다.

Positive QEMU evidence는 firmware entry, explicit product graph, payload/World loading,
final map, `ExitBootServices`, Ribon transfer, LUCA locore/kernel main, external initial-user
runtime, system-running과 실제 EL0 shell input-ready를 같은 raw serial에 순서대로 보존한다.
Kernel, World, manifest, ESP, firmware와 별도 ext2 fixture digest는 실행 전후 비교한다.

이 결과의 evidence class는 integrated virtual development boot다. Secure Boot, rollback,
physical board, UTM GUI, GPT disk-only boot, graphical display, keyboard input 또는 ext2
durability를 열지 않는다. R25가 disk composition을, R26–R28이 input/terminal/display를,
R29가 실제 UTM GUI를 별도로 검증한다.
