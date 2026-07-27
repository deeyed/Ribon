---
doc_type: canonical
status: accepted
authority: normative
last_verified: 2026-07-27
code_paths:
  - include/Ribon/
  - sdk/
  - src/common/
  - src/arch/
  - src/environments/
  - src/image-formats/
  - src/protocols/
  - platforms/
  - products/
  - targets/
tests:
  - ribon-library-boundary-lint
  - ribon-plugin-graph-lint
  - ribon-product-composition-test
  - ribon-docs
hardware:
  - none
supersedes:
  - core-profile-platform architecture
  - monolithic frontend architecture
---

# Ribon 구조와 최종 비전

Ribon은 architecture, firmware, board, OS에 종속되지 않는 결정론적 boot runtime
library다. Ribon Core와 Boot Library는 정적으로 검증된 plugin을 조합하여 독립 실행
bootloader, 기존 firmware 위의 boot application, firmware product를 생성한다.

Ribon의 독립성은 저장소에 architecture 또는 OS 코드가 없다는 뜻이 아니다. Generic
library와 public plugin ABI가 native ABI를 알지 않고, 선택된 component만 해당 ABI를
소유한다는 뜻이다.

## 제품 정체성

Ribon 저장소는 다음 산출물을 독립된 product로 제공한다.

| Product | 목적 | 소유하지 않는 것 |
| --- | --- | --- |
| `libribon-core` | 상태 전이, memory map, capability, fixed arena | entry, OS handoff, firmware native ABI |
| `libribon-boot` | boot source, image load, plan, commit, transfer orchestration | OS별 wire format, board MMIO |
| `libribon-sdk` | plugin package ABI, host contract harness, firmware service publication | 선택된 plugin 정책과 native firmware 구현 |
| `ribon-bootmgr` | 독립 실행 boot manager | 특정 OS의 고정 의미론 |
| environment application | UEFI, BIOS, SBI, raw-FDT를 Ribon service로 변환 | firmware service 제공자 역할 |
| firmware product | 선택된 firmware personality와 driver를 조립 | Ribon Core의 OS 특수화 |

하나의 product가 다른 product의 권한을 암묵적으로 획득하지 않는다. UEFI application은
UEFI firmware를 소비하며 UEFI firmware를 구현하지 않는다. UEFI firmware product는
별도의 personality와 lifecycle을 통해 UEFI-compatible service를 제공한다.

## 계층

### Core Library

Core Library는 다음만 소유한다.

- caller-owned fixed arena
- normalized physical memory map
- immutable product 및 plugin descriptor 검증
- bounded state transition과 failure vocabulary
- capability graph와 operation deadline
- caller-owned `RibonBootTransaction` stage와 terminal failure receipt

Core는 OS protocol, executable format, architecture instruction, firmware header,
device MMIO, network packet을 해석하지 않는다. Core에는 `main`, `_start`, selected
protocol global, native firmware handle이 없다.

### Boot Library

Boot Library는 OS 중립적인 boot orchestration을 소유한다.

- boot source의 bounded byte-range read와 product retry budget
- read-only block, partition, filesystem와 configuration candidate의 bounded resolution
- image format plugin을 통한 load plan
- component와 destination overlap 검증
- trust result와 provenance의 immutable snapshot
- slot 선택과 journal service 호출 순서
- 선택된 boot protocol의 prepare와 handoff 호출
- metadata write/flush commit, selected environment quiesce 뒤 architecture transfer 호출

Boot Library는 RPH1, Linux `boot_params`, FreeBSD metadata, Multiboot tag를 직접 알지
않는다.

### Boot Protocol

Boot Protocol은 OS 또는 다음 실행 단계가 요구하는 부팅 계약이다. Protocol은 기존
`Profile` 경계를 대체한다.

Protocol은 다음을 소유한다.

- kernel과 component 역할
- protocol manifest와 configuration 의미론
- 허용 executable 또는 image format 조합
- handoff wire artifact
- architecture별 register ABI와 entry precondition
- boot confirmation의 OS별 payload 의미론

Parus Handoff, Linux boot protocol, FreeBSD boot metadata, Multiboot, EFI chainload는
서로 다른 protocol plugin이다. Protocol은 block, network, watchdog, firmware
protocol, MMIO를 직접 호출하지 않는다.

### Architecture Backend

Architecture Backend는 CPU와 instruction ABI만 소유한다.

- executable machine 및 canonical address 검증
- cache와 instruction synchronization
- privilege state와 interrupt mask 정규화
- 최소 translation bridge
- protocol이 선택한 register ABI 적용
- terminal transfer, halt, reset primitive

Permanent OS page table, runtime interrupt policy, SMP runtime, board resource map은
architecture backend의 boot 책임이 아니다.

### Environment Consumer

Environment Consumer는 이미 존재하는 실행환경을 Ribon service로 변환한다.

- `uefi-app`: UEFI Boot Services와 protocol 소비
- `bios-client`: BIOS interrupt와 table 소비
- `raw-fdt`: 초기 register와 FDT 소비
- `sbi`: SBI service와 FDT 소비
- `host`: 테스트용 caller service 소비

Environment native type은 adapter 밖으로 나가지 않는다. UEFI handle, BIOS register
frame, SBI extension ID는 generic Core 또는 Boot Protocol에 노출하지 않는다.

### Firmware Personality

Firmware Personality는 Ribon service를 외부 firmware ABI로 제공하는 선택적 product
계층이다.

- UEFI personality는 System Table, Boot Services, 선택적 Runtime Services를 제공한다.
- BIOS personality는 명시적으로 선택된 legacy interrupt와 table ABI를 제공한다.
- 다른 personality는 별도 specification과 product contract를 가진다.

Handle/protocol database, event dispatch, variable service처럼 firmware ABI가 요구하는
registry는 personality가 소유한다. Generic Core는 범용 service locator를 제공하지
않는다.

### Driver와 Service Plugin

Driver와 Service Plugin은 재사용 가능한 기능을 제공한다.

- block, filesystem, console, timer
- network transport
- executable and image format
- cryptographic verification
- update journal과 metadata
- watchdog와 reset reason

Read-only boot media parser는 generic Boot Library extension이다. GPT/MBR, FAT32와
configuration grammar는 OS protocol, native firmware handle, board MMIO와 update writer를
해석하지 않는다. Native file/block controller는 Environment Consumer가 typed source 또는 block
provider로 변환한다.

Board는 resource와 wiring을 기술하고 driver 구현을 복제하지 않는다. Driver는 특정
board 이름을 policy 분기로 사용하지 않는다.

### Product와 Target

Product manifest는 기능 조합을 정의하고 Target은 실행 가능한 구체 조합을 정의한다.

```text
product
  = core + boot manager + protocol set + policy set

target
  = product + architecture + environment or firmware personality
            + platform + image recipe + evidence policy
```

QStar는 검증된 조합만 허용하고 immutable plugin registry, typed service directory와
product descriptor를 생성한다.
Source scan, weak symbol, constructor side effect로 plugin을 발견하지 않는다.

Boot media recipe는 configuration file path, payload path, bounded parser limits와 source authority를
명시할 수 있다. Recipe가 embedded fixture를 사용할 때에는 fixture-only product identity를 가지며
external media product의 compatibility alias가 아니다.

## Typed Service Graph

Generic Core는 서비스별 giant callback struct나 전역 service locator를 소유하지 않는다.
QStar manifest는 provider가 export한 `RibonServiceDescriptor`를 stable ID 순으로
열거하고, 이를 caller-owned immutable `RibonServiceDirectory`로 생성한다. Context는
registry와 directory를 allocation 없이 함께 검증한다.

각 service descriptor는 typed role, stable ID, capability와 operation ABI, compatibility
mask, 최초 소비 phase, native handle lifetime, arena/input/output/deadline budget, authority
또는 collection cardinality를 명시한다.

Boot source, timer, reset처럼 한 owner만 허용하는 role은 authority다. Transport,
filesystem, diagnostic sink처럼 여러 static provider를 포함할 수 있는 role은 collection이다.
Collection이 둘 이상이면 product의 explicit selection이 boot 전에 active owner를 고정한다.
Core는 duplicate authority, missing authority, unselected ambiguous collection, phase inversion,
ABI mismatch와 budget/mode violation을 fail-closed한다.

## Plugin 종류

활성 plugin descriptor ABI는 다음 종류를 구분한다.

| Kind | 예 |
| --- | --- |
| `ARCHITECTURE` | x86_64, AArch64, RISC-V 64 |
| `ENVIRONMENT` | UEFI application, BIOS client, raw-FDT, host |
| `IMAGE_FORMAT` | ELF64, PE/COFF |
| `BOOT_PROTOCOL` | Parus, synthetic contract fixture |
| `PLATFORM` | QEMU virt, RPi5, PC UEFI, PC BIOS |

`POLICY`와 `FIRMWARE_PERSONALITY`는 firmware product가 사용할 수 있는 활성 kind다.
`SERVICE`는 SDK package가 제공하는 typed service operation의 활성 kind다. Mode
descriptor는 plugin descriptor와 별도 계약이다.

Driver, filesystem, transport, security와 firmware service는 SDK 확장에서 독립 kind가
될 수 있다. 해당 kind가 public descriptor ABI에 추가되기 전에는 product가 선택한
private object이며 runtime plugin으로 간주하지 않는다.

향후 kind의 역할 예시는 다음과 같다.

| 확장 경계 | 예 |
| --- | --- |
| `DRIVER` | PL011, block controller, NIC |
| `FILESYSTEM` | FAT, ISO9660 |
| `TRANSPORT` | firmware HTTP, TFTP, native Ethernet |
| `SECURITY` | digest, signature, anti-rollback provider |
| `UPDATE_POLICY` | slot journal과 recovery transition |
| `BOOT_POLICY` | entry selection과 retry |
| `FIRMWARE_SERVICE` | variable, time, reset, image, protocol database |

Plugin은 build-time에 선택되는 정적 component다. Runtime-loadable plugin은 별도의
signature, relocation, W^X, dependency authenticity, rollback, lifetime 계약이 승인되기
전에는 지원하지 않는다.

## 설치 가능한 SDK

SDK install tree는 `include/Ribon/`, `libribon-core.a`, `libribon-boot.a`,
`libribon-sdk.a`, ABI symbol allowlist, composition schema와 package template로
구성된다. 외부 consumer와 plugin package는 source checkout의 private header나 generated
registry에 의존하지 않는다.

`libribon-sdk`는 SDK ABI tuple, package descriptor validation, host package contract와
firmware personality service directory를 제공한다. Architecture, environment, boot
protocol, platform implementation은 해당 archive에 포함하지 않는다.

## Plugin graph 불변식

각 plugin descriptor는 ABI version, kind, stable ID, init phase, provided capability,
required capability, memory budget, operation deadline, operation table을 선언한다.

QStar composition은 다음을 fail-closed로 거부한다.

- duplicate stable ID 또는 authority provider
- collection owner selection 없는 ambiguous provider
- 지원되지 않는 ABI major
- 충족되지 않는 required capability
- dependency cycle과 phase 역전
- architecture, environment, product의 불가능한 조합
- normal product에 recovery network 또는 inactive-slot writer 포함
- budget을 초과하는 arena, input, output
- product에서 선택하지 않은 board, protocol, personality object

Registry는 generated C source와 descriptor로 고정하며 실행 중 임의로 확장하지 않는다.

## Lifecycle

Bootloader product는 다음 lifecycle을 따른다.

```text
native entry
  -> architecture early normalization
  -> environment capture
  -> product and plugin graph validation
  -> platform facts and services freeze
  -> boot policy and source selection
  -> image load and trust validation
  -> boot protocol handoff preparation
  -> journal commit and environment quiesce
  -> architecture transfer
```

Boot Library transaction은 `CAPTURE -> VALIDATE_PRODUCT -> FREEZE_PLATFORM_FACTS ->
SELECT_SOURCE -> VERIFY_MANIFEST -> LOAD_IMAGE -> PREPARE_PROTOCOL -> COMMIT_ATTEMPT ->
QUIESCE_ENVIRONMENT -> TRANSFER` 순서를 가진다. Stage failure는 pointer-free receipt로
종료하며 generic library가 fallback source, resident overseer 또는 OS runtime을 시작하지
않는다.

Firmware product는 다음 확장 phase를 사용할 수 있다.

```text
EARLY -> FOUNDATION -> DRIVER -> BOOT -> QUIESCE -> RUNTIME
```

`RUNTIME`은 firmware personality가 명시적으로 제공한 service만 유지한다. Ribon
bootloader product는 커널 entry 뒤 상주하는 hypervisor가 아니다.

## OS 독립성과 Parus

Parus protocol은 Ribon plugin ABI를 소비하는 한 개의 OS-specific component다. RPH1,
Parus entry flag, Parus confirmation semantics는 Parus protocol 밖으로 나오지 않는다.

Parus overseer, fleet update policy, health policy는 generic Core가 아니라 OS-specific
policy plugin 또는 companion package가 소유한다. Linux와 FreeBSD product에는 해당
object가 링크되지 않는다.

OS 독립성은 최소 두 개의 서로 다른 protocol product와 protocol-free library embed
test로 검증한다. Parus-only fixture 성공은 OS 독립성의 충분한 증거가 아니다.

## Board와 실행환경

RPi5는 architecture나 firmware 종류가 아니다. 다음 component 조합이다.

```text
AArch64 + raw-FDT 또는 VideoCore environment
        + BCM2712/RPi5 platform
        + Raspberry Pi image recipe
```

QEMU `virt`는 별도의 machine target이다. RPi5와 QEMU는 AArch64, FDT, PL011 같은
component를 공유할 수 있지만 identity, memory fallback, linker, package, evidence
claim을 공유하지 않는다.

## 결정성

Parser, loader, plugin init, network transaction, journal operation은 다음 상한을
descriptor로 선언한다.

- 입력과 출력 byte 수
- table, component, plugin 수
- arena 사용량과 alignment
- retry 수
- operation deadline
- quiesce와 transfer 전 허용 상태

Normal boot는 network 가용성에 의존하지 않는다. 손상된 입력, 미지원 capability,
불완전한 plugin graph는 명시적 오류로 종료한다.

## 비목표

Generic Ribon Core는 다음을 소유하지 않는다.

- 특정 OS의 permanent virtual-memory 정책
- Parus scheduler, executor, driver 정책
- Linux 또는 FreeBSD runtime policy
- fleet rollout과 장기 repository client
- actuator와 flight-control 안전
- 범용 shell 또는 server
- 장기 resident hypervisor
- UEFI 또는 BIOS 전체 specification의 무조건적 기본 구현
