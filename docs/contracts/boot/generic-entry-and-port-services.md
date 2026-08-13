---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-29
code_paths:
  - include/Ribon/arch/entry.h
  - include/Ribon/arch/ops.h
  - include/Ribon/protocol/entry_contract.h
  - include/Ribon/service/directory.h
  - include/Ribon/port/
  - src/arch/
  - src/protocols/os/
  - ports/
tests:
  - ribon-protocol-contract-tests
  - ribon-arch-ops-tests
  - ribon-port-service-tests
hardware:
  - none
supersedes:
  - platform facts entry contract
---

# Generic entry와 port service 계약

## 호출 방향

```text
firmware native entry
  -> source-neutral native tuple normalization
  -> environment capture
  -> generated product selection
  -> image-format analysis
  -> OS protocol handoff
  -> OS protocol entry invocation
  -> ISA prepare_entry
  -> environment quiesce
  -> ISA transfer_prepared
```

Boot manager와 environment는 OS wire를 parse하지 않는다. Architecture backend는
register argument의 의미를 해석하지 않는다. OS protocol은 port I/O, MMIO address,
firmware handle lifetime 또는 page-table encoding을 소유하지 않는다.

Raw-FDT의 native tuple은 `boot_cpu_id`와 `machine_description_address`다. AArch64
firmware의 FDT-only `x0` 입력은 target entry가 `(0, x0)`으로 정규화하고, SBI의
`a0/a1`은 hart ID와 FDT로 보존한다. 이 tuple은 firmware/environment 입력이며 Parus,
Linux, Zircon kernel register ABI와 동일한 것으로 해석하지 않는다.

## Entry invocation

`RibonEntryInvocation`은 다음을 모두 만족해야 한다.

- ABI version, architecture와 register ABI가 서로 일치한다.
- entry address는 image-format과 protocol이 승인한 executable entry다.
- argument count는 architecture-neutral 고정 상한 이하다.
- interrupt, privilege와 translation precondition이 명시돼 있다.
- sealed invocation의 argument 의미는 protocol package만 정의한다.

Architecture `prepare_entry`는 invocation을 검증해 `RibonPreparedEntry`를 만든다.
Terminal `transfer_prepared`는 prepared object만 소비하며 OS별 flag를 받지 않는다.

## Port service

Port service는 machine wiring을 다음 role로 분리한다.

| Role | Authority |
| --- | --- |
| diagnostic sink | bounded early byte write |
| machine description | native FDT/ACPI input capacity와 stable machine ID |
| payload placement | 허용 physical window와 bounded placement |
| timer | monotonic counter frequency와 read |
| reset/watchdog | 해당 product가 명시적으로 요청한 경우에만 제공 |

각 authority role은 service directory에서 정확히 한 provider만 허용한다. 한 role이
없으면 그 role을 요구하지 않는 environment/product는 유효하다. Core는 특정 board,
QEMU machine 또는 mandatory platform profile을 요구하지 않는다.

## OS package

OS package는 공식 wire/entry 계약을 소유한다.

- Parus: RLH1 build/parse와 Parus register invocation
- Linux: Linux boot protocol validation과 architecture별 native argument
- FreeBSD: EFI chainload 또는 architecture별 loader metadata
- Zircon: bounded ZBI validation과 kernel entry invocation

OS package는 environment와 port symbol을 직접 참조하지 않는다. Runtime claim은 해당
OS payload를 실제 실행한 evidence class에서만 연다.

## 실패

Unknown protocol/image ID, ambiguous provider, ABI mismatch, unsupported ISA/OS tuple,
payload-window violation과 native-input truncation은 terminal preparation failure다.
다른 OS protocol, 다른 port 또는 legacy platform path로 암묵 fallback하지 않는다.

## Supervised product evidence

Generic target marker만으로 external kernel 실행 성공을 주장하지 않는다. Parus-specific
product smoke는 Ribon environment, product graph, protocol handoff와 transfer marker 뒤
LOCORE, STAGE0, XIBALBA, EB0~EB9, KMAIN, IDLE을 순서대로 정확히 한 번 요구한다.
Required marker 목록은 중복 입력을 canonicalize하며 panic, exception 또는 payload
boot-marker failure를 terminal failure로 분류한다. Result는 payload/image/firmware hash,
QEMU command, first divergence, raw serial과 process-group cleanup을 보존한다.
