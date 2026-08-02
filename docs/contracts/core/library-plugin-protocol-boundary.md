---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-08-02
code_paths:
  - include/Ribon/core/
  - include/Ribon/boot/
  - include/Ribon/plugin/
  - include/Ribon/protocol/
  - src/common/
  - src/protocols/
tests:
  - ribon-library-boundary-lint
  - ribon-plugin-descriptor-tests
  - ribon-protocol-contract-tests
hardware:
  - none
supersedes:
  - core-profile-platform-boundary
---

# Library·Plugin·Protocol 경계 계약

이 계약은 embeddable Ribon library, plugin ABI, OS Boot Protocol의 호출 방향과
ownership을 고정한다.

## Library context

Library 호출자는 product descriptor, immutable typed service directory, caller-owned arena를
제공한다.
초기화는 입력 전체를 검증한 뒤 immutable context를 만든다.

Context 검증 전에는 service callback, protocol operation, architecture operation을
호출하지 않는다. Context는 hidden heap, global selected protocol, native firmware
pointer를 소유하지 않는다.

Library public operation은 다음 phase를 구분한다.

| Operation | 효과 |
| --- | --- |
| `context_initialize` | descriptor, service, arena를 검증한다 |
| `boot_transaction_initialize` | validated context에서 exact service authority를 고정한다 |
| `boot_transaction_prepare` | source read, image plan, protocol handoff를 caller-owned storage에 만든다 |
| `boot_transaction_commit_attempt` | metadata write와 flush를 durable boundary로 만든다 |
| `boot_transaction_refresh_after_commit` | final platform fact로 handoff만 다시 만든다 |
| `boot_transaction_quiesce_environment` | selected native closure authority를 실행한다 |
| `boot_transaction_transfer` | architecture register ABI를 적용하고 반환하지 않는다 |

Transaction input의 environment, source, payload buffer, normalized memory map, image layout,
handoff buffer와 artifact storage는 caller가 transfer까지 소유한다. Boot Library는 heap을
할당하지 않고 native pointer를 receipt나 plan에 저장하지 않는다.

`boot_transaction_prepare` 실패는 durable state를 변경하지 않는다.
`boot_transaction_commit_attempt` 뒤 실패는 명시적인 recovery transition을 요구하며
prepare 단계로 암묵 복귀하지 않는다. API는 `RibonBootSession`, `RibonBootRequest`,
`boot_prepare`, `boot_commit`, `environment_quiesce`, `boot_transfer` compatibility alias를
제공하지 않는다.

`RibonBootFailureReceipt`는 failure stage, stable reason, static provider ID, consumed byte,
component, retry budget을 보존한다. Receipt는 terminal failure에만 읽을 수 있고 native
handle, heap pointer, firmware pointer를 포함하지 않는다.

`environment-quiesce` callback은 non-blocking이고 bounded여야 한다. Closure는 timer
authority를 함께 철회할 수 있으므로 Boot Library는 callback 전후에 timer를 재호출하지
않는다. Source read, protocol handoff, metadata write/flush는 selected timer authority로
deadline을 검사한다.

## Plugin descriptor

Descriptor는 다음 필드를 가진다.

- magic과 descriptor size
- plugin ABI major와 minor
- stable plugin ID와 kind
- supported lifecycle phase
- provided와 required capability
- architecture, environment, product compatibility
- arena와 input/output budget
- operation deadline
- typed operation table

ABI major가 다르면 link 또는 product validation을 거부한다. Minor version은 descriptor
size와 capability negotiation으로 확장하며 알 수 없는 required capability를 거부한다.

Callback 존재 여부와 capability bit는 정확히 일치해야 한다. Callback만 있거나 bit만
선언된 descriptor는 유효하지 않다.

## Registry

QStar는 product manifest에 열거된 plugin으로 registry source를 생성한다. Registry
순서는 stable plugin ID와 phase dependency로 결정한다.

다음을 허용하지 않는다.

- directory scan 또는 filename suffix에만 의존한 provider 발견
- weak symbol fallback
- constructor가 만드는 hidden registration
- normal product에서 runtime plugin discovery
- product manifest에 없는 object의 link

## Boot Protocol operation

Boot Protocol은 다음 operation을 제공한다.

| Operation | 계약 |
| --- | --- |
| `match` | protocol ID와 ABI 범위를 판별한다 |
| `validate_components` | kernel, module, DTB, command line 역할을 검증한다 |
| `select_image_formats` | 허용 image format plugin을 제한한다 |
| `prepare_handoff` | Direct protocol만 caller-owned 고정 용량 buffer에 wire artifact를 생성한다 |
| `prepare_terminal` | Direct register invocation 또는 managed image requirement 중 하나를 봉인한다 |
| `validate_confirmation` | OS-specific confirmation payload를 검증한다 |

`RibonBootProtocol.terminal_execution`은 `DIRECT_ENTRY` 또는
`FIRMWARE_MANAGED_IMAGE` 중 정확히 하나다. Direct protocol은 handoff capability와 callback을 가져야
하고 managed protocol은 둘 다 가지면 안 된다. Managed request는 native provider handle을 포함하지
않으며 direct entry storage가 전부 0이어야 한다.

Protocol은 I/O, crypto, watchdog, update journal, firmware native service를 직접 호출하지
않는다. 필요한 결과는 Boot Library가 검증한 immutable descriptor로 받는다.

Protocol의 architecture-specific 코드는 해당 protocol의 `arch/<arch>/` 아래에
격리한다. Generic protocol code에 board 또는 firmware 조건문을 두지 않는다.

Boot manager와 environment frontend는 generated registry/configuration으로 protocol을
선택하며 Parus, RPH1, Linux, ZBI 같은 OS-specific symbol과 parser를 직접 참조하지 않는다.

## Image format 분리

ELF, PE/COFF, Linux Image, Multiboot header parser는 image-format plugin이다. OS
protocol은 허용 format을 선택하지만 parser 구현을 복제하지 않는다.

Image-format plugin은 destination range와 entry candidate를 반환하며 최종 register ABI를
결정하지 않는다. Protocol과 architecture backend가 entry contract를 함께 검증한다.

## Typed service directory

Library는 다음 service를 typed operation으로 소비한다.

- boot source read
- memory allocation reservation
- monotonic timer
- environment quiesce
- diagnostic sink
- metadata read/write/flush
- inactive storage write/erase
- random nonce
- network transport
- cryptographic verify
- watchdog와 reset reason

Service native handle은 typed operation context 안에 남는다. `RibonServiceDescriptor`는
stable ID, role, capability, lifecycle phase, lifetime, compatibility mask, budget, operation
ABI와 validator를 함께 고정한다. `RibonServiceDirectory`는 caller-owned pointer array이며
Core가 검증 중 heap allocation, probe 또는 callback을 수행하지 않는다.

Authority role은 정확히 한 provider만 가진다. Collection role은 여러 static provider를
가질 수 있지만 둘 이상일 때 product manifest의 selection이 exact stable ID를 고정해야
한다. Plugin capability dependency가 collection provider에 의존하면 같은 product selection
없이는 ambiguous로 거부한다.

## Allocation과 interrupt

Normal boot는 caller-owned fixed arena만 사용한다. Arena는 rewind와 free를 제공하지
않으며 overflow, capacity, non-power-of-two alignment를 allocation 전에 거부한다.

Kernel transfer 전 interrupt는 masked 상태를 유지한다. Environment 또는 driver가
completion interrupt를 사용해도 Library에 interrupt ownership을 이전하지 않는다.

## Object graph

다음 archive와 product 경계를 집행한다.

```text
libribon-core
  excludes arch, environment, protocol, board, firmware personality

libribon-boot
  excludes OS wire implementation and native firmware types

protocol archive
  excludes environment, board, driver implementation

environment archive
  excludes OS protocol and firmware-provider personality

product
  contains exactly one architecture and one entry environment
```

Normal, recovery, provisioning, diagnostic product는 서로 다른 policy graph다. Recovery
network, inactive-slot writer, diagnostic fixture는 normal product에 링크하지 않는다.

## 실패 규칙

다음은 fail-closed 오류다.

- descriptor magic, size, ABI 불일치
- duplicate authority 또는 stable ID
- required capability 부재
- unselected ambiguous collection, phase dependency cycle 또는 phase inversion
- budget 초과
- source와 destination overlap
- handoff capacity 초과
- quiesce 뒤 firmware service 재호출
- 선택하지 않은 fallback protocol 실행
- retry budget 또는 operation deadline 초과
- partial metadata write 또는 flush failure 뒤 transfer

Malformed protocol artifact를 다른 protocol 입력으로 재해석하지 않는다.
