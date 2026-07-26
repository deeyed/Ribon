---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-26
code_paths:
  - include/Ribon/
  - src/core/
  - src/arch/
  - src/firmware/
  - src/profiles/
tests:
  - core_service_boundary_tests
  - arch_ops_tests
  - mode_descriptor_tests
  - object_graph_lint
hardware:
  - none
supersedes:
  - profile-defined handoff-only boundary
---

# Core·Profile·Platform 경계 계약

이 계약은 Ribon Core, OS profile, architecture backend, platform adapter, 공통 service의
소유권과 호출 방향을 고정한다.

## Core 입력

Core는 다음 typed descriptor만 소비한다.

- `RibonPlatformFacts`: firmware, board 후보, timer, reset reason, capability bitset
- `RibonMemoryMap`: normalized physical range
- `RibonBootSource`: block, file, memory, network source
- `RibonSlotSet`: slot metadata의 검증된 snapshot
- `RibonProfileOps`: 선택된 OS profile operation table
- `RibonArchOps`: 선택된 architecture operation table

Platform native pointer는 adapter 밖에서 직접 해석하지 않는다. Borrowed pointer가
필요하면 주소, 길이, alignment, lifetime, reclaim 조건을 함께 가진 descriptor로
전달한다.

`RibonCoreContext`는 mode, platform operation table, architecture operation table,
profile, 비어 있는 fixed arena를 한 번에 검증한다. 검증이 성공하기 전에는 operation을
호출할 수 없다. Profile이 mode를 지원하지 않거나 mode의 required capability가 없거나
forbidden capability가 link graph에 들어오면 검증은 fail-closed다.

## Profile operation

OS profile은 다음 operation을 제공한다.

| Operation | 계약 |
| --- | --- |
| `match_manifest` | profile ID와 ABI 범위를 판별한다 |
| `validate_components` | OS가 요구하는 component 조합과 역할을 검증한다 |
| `select_entry_contract` | 허용 entry mode와 register ABI를 선택한다 |
| `build_handoff` | caller-owned 고정 용량 buffer에 wire artifact를 생성한다 |
| `validate_confirmation` | boot nonce와 generation을 포함한 확인 record를 검증한다 |

Profile은 block I/O, network I/O, cryptographic primitive, watchdog register를 직접
호출하지 않는다. 필요한 결과는 Core가 검증한 descriptor로 받는다.

`RibonProfile`은 operation마다 capability bit를 가진다. Capability bit와 callback
존재 여부는 정확히 일치해야 한다. Parus profile ABI v1은 정확히 한 kernel component를
요구하고 boot module과 device tree component를 허용한다. Component 수는 32개 이하이며
크기는 0보다 커야 하고 R2의 reserved flag는 0이어야 한다. Entry contract는 다음과 같다.

| Architecture | Register ABI | 필수 entry flag | 허용 entry flag |
| --- | --- | --- | --- |
| x86_64 | `RDI=RPH1`, `RSI=flags` | `RPH1` | `RPH1`, `ENTERED_HIGH`, `DIRECT_HIGH` |
| AArch64 | `X0=RPH1`, `X1=flags` | `RPH1` | `RPH1`, `ENTERED_HIGH`, `DIRECT_HIGH` |
| RISC-V 64 | `A0=RPH1`, `A1=flags` | `RPH1` | `RPH1` |

Boot confirmation semantic descriptor는 profile ID, generation, 32-byte nonce, healthy
result를 가진다. Profile은 모든 nonce byte를 비교하고 generation과 profile ID까지
일치할 때만 이를 승인한다. 이 구조체는 durable wire record가 아니다. Wire encoding,
checksum, 서명, torn-write 처리는 update 계약이 소유한다.

## Platform operation

Platform adapter는 다음 capability를 독립적으로 제공한다.

- boot source read
- inactive slot write, erase, flush
- monotonic timer와 deadline
- watchdog arm과 reset
- persistent metadata read/write
- packet 또는 firmware network transport
- random nonce
- diagnostic sink

`RibonPlatformOps`는 모든 callback을 non-null로 유지한다. Adapter는
`ribon_platform_ops_init_unsupported`로 전체 table을 초기화하고, 지원 operation의
callback을 교체하면서 해당 bit를 `unsupported`에서 `supported`로 옮긴다. Bit만
승격하거나 callback만 교체한 table은 유효하지 않다. Core는 capability 확인 전에
operation을 호출하지 않는다.

Mode별 Platform capability 계약은 다음과 같다.

| Mode | Required | Forbidden |
| --- | --- | --- |
| normal | boot source read, monotonic timer | inactive write, inactive erase, network |
| recovery | boot source read, inactive write/erase, flush, timer, metadata, nonce | 없음 |
| provisioning | inactive write, flush, timer, metadata, nonce | 없음 |
| diagnostic | timer, diagnostic sink | inactive write, inactive erase, persistent metadata |

Recovery의 network capability는 허용되지만 필수는 아니다. 로컬 복구 media만 있는
platform도 recovery graph를 제공할 수 있다.

## Architecture operation

Architecture backend는 다음을 소유한다.

- executable machine과 canonical address 검증
- cache 및 instruction synchronization
- privilege level 정규화
- 최소 entry bridge
- register ABI 적용
- terminal halt와 reset primitive

Permanent OS page table, runtime interrupt controller, secondary CPU runtime 정책은
architecture backend의 부트 책임이 아니다.

`RibonArchOps`는 payload machine/canonical address 검증, cache synchronization,
privilege normalization, direct-high preparation, entry bridge, terminal halt, reset을
각 capability로 선언한다. Payload validation과 halt는 모든 backend의 필수 operation이다.
Capability가 없는 optional architecture callback은 호출하지 않는다. Direct-high
capability가 없는 RISC-V backend는 direct-high callback을 table에 노출하지 않는다.

Mode별 Architecture capability 계약은 다음과 같다.

| Mode | Required | Forbidden |
| --- | --- | --- |
| normal | payload validation, cache sync, entry bridge, halt | 없음 |
| recovery | payload validation, cache sync, entry bridge, halt | 없음 |
| provisioning | payload validation, halt | 없음 |
| diagnostic | payload validation, halt | 없음 |

RISC-V 64 backend는 R2 operation table에서 entry bridge capability를 선언하지 않는다.
따라서 normal/recovery `RibonCoreContext` 검증은 RISC-V entry bridge가 구현될 때까지
fail-closed다.

## Allocation과 interrupt

Normal boot의 Core와 profile은 `RibonArena` 고정 bump arena만 사용한다. Arena는
caller-owned storage를 빌리고 allocation을 초기화하지 않으며 rewind나 free를
제공하지 않는다. 크기가 0이거나 2의 거듭제곱이 아닌 alignment는 거절한다. Pointer
overflow와 capacity 초과는 allocation 전 검사하고 `OUT_OF_CAPACITY`를 반환한다.
Parser와 builder는 caller-owned capacity를 넘지 않으며 out-of-capacity를 명시적으로
반환한다. OS entry 전까지
interrupt는 masked 상태를 유지한다. Network recovery가 polling 외의 completion
mechanism을 요구하면 platform adapter가 이를 내부에서 정규화하되 Core에 interrupt
ownership을 노출하지 않는다.

Mode descriptor의 고정 상한은 다음과 같다.

| Mode | memory regions | load segments | components | retries | input | handoff | arena | operation deadline |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| normal | 256 | 32 | 32 | 2 | 64 MiB | 64 KiB | 256 KiB | 30 s |
| recovery | 256 | 32 | 32 | 4 | 256 MiB | 64 KiB | 512 KiB | 120 s |
| provisioning | 256 | 32 | 32 | 3 | 256 MiB | 64 KiB | 512 KiB | 120 s |
| diagnostic | 512 | 64 | 64 | 2 | 64 MiB | 64 KiB | 1 MiB | 60 s |

Operation deadline은 millisecond duration 상한이다. Platform timer frequency를 사용한
absolute tick 변환은 overflow를 검사해야 한다.

## Object graph

Normal, recovery, provisioning, diagnostic binary는 `src/modes/` 아래에서 정확히 한
mode source를 링크한다. 각 source는 같은 `ribon_mode_selected` symbol을 정의하므로
둘 이상을 링크하면 link-time 오류가 발생한다. Host archive는 host adapter만 포함하며
UEFI, BIOS, Raspberry Pi adapter object를 포함하지 않는다. UEFI와 Raspberry Pi normal
frontend는 normal mode source만 링크한다.

Recovery network와 diagnostic fixture가 normal image에 암묵적으로 링크되면 경계
위반이다. `object_graph_lint`는 네 mode archive가 선택 mode object 하나만 포함하고
host archive에 다른 platform object가 없는지 검사한다.
