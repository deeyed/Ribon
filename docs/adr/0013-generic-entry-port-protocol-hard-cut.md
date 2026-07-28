---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-29
code_paths:
  - include/Ribon/arch/
  - include/Ribon/boot/
  - include/Ribon/port/
  - include/Ribon/protocol/
  - src/arch/
  - src/environments/
  - src/protocols/os/
  - ports/
  - products/bootmgr/
tests:
  - make check
  - make qemu-aarch64-virt-parus-smoke
  - make qemu-riscv64-virt-parus-smoke
  - make x86_64-uefi-app-smoke
hardware:
  - Raspberry Pi 5 fresh UART is required for a physical-hardware claim
supersedes:
  - mandatory platform plugin and RibonPlatformFacts
  - frontend-owned Parus and RPH1 selection
  - protocol meaning in the generic architecture entry layer
---

# ADR: generic entry, machine port와 OS protocol을 hard cut한다

## 맥락

기존 consumer product는 모든 board, firmware, image placement와 diagnostic 정보를
`RibonPlatformFacts` 하나에 넣고 정확히 하나의 platform plugin을 요구했다. Raw-FDT와
UEFI frontend는 Parus descriptor와 ELF descriptor를 직접 참조하고 RPH1을 직접
검사했다. Generic architecture layer에도 RPH1 flag가 들어 있었다.

이 구조는 다음 문제를 만든다.

- Ribon Core가 board profile의 존재와 수를 정책으로 강제한다.
- firmware ABI, machine wiring, OS handoff와 ISA register ABI가 서로 독립적으로
  교체될 수 없다.
- 같은 UEFI 또는 raw-FDT environment에서 Linux, FreeBSD, Zircon 같은 다른 OS
  protocol을 선택할 수 없다.
- AArch64 raw entry의 native argument 위치를 target마다 다르게 해석할 수 있다.
- 새 RISC-V machine 또는 새 firmware provider가 기존 platform 구조를 복제하게 된다.

## 결정

1. Architecture layer는 OS 의미를 모르는 `RibonEntryInvocation`을 검증하고,
   ISA register ABI로 변환한 `RibonPreparedEntry`만 terminal transfer한다.
   RPH1, DTB, ZBI, Linux boot params와 EFI image handle 의미는 OS protocol이 소유한다.
2. Raw-FDT environment의 source-neutral native tuple은
   `(boot_cpu_id, machine_description_address)`다. AArch64 firmware가 `x0`에 FDT를
   주면 target entry가 `(0, x0)`으로 정규화하고, OpenSBI의 `(a0 hartid, a1 FDT)`는
   같은 tuple로 보존한다. 이후 kernel register 의미는 OS protocol invocation만
   정의한다.
3. Core와 product graph는 platform plugin을 요구하지 않는다.
   Machine-dependent wiring은 `ports/<provider>/<machine>/`에 격리한다.
4. Port는 한 개의 monolithic fact table을 Core에 제공하지 않는다. 필요한 권한은
   diagnostic sink, machine-description input, payload-placement window 같은 typed
   service descriptor로 각각 제공한다. Service가 필요 없는 표준 firmware product는
   board port 없이 조합할 수 있다.
5. Environment는 UEFI, raw-FDT, SBI, BIOS 같은 firmware-facing lifetime과 native
   service closure를 소유한다. Board 이름이나 OS protocol은 environment에 들어가지 않는다.
6. Boot manager는 generated registry와 configuration의 stable plugin ID로 protocol과
   image-format을 선택한다. Parus symbol, RPH1 parser 또는 ELF symbol을 frontend에서
   직접 참조하지 않는다.
7. OS integration은 `src/protocols/os/<os>/`와
   `include/Ribon/protocols/os/<os>/`에 둔다. Parus는 첫 runtime provider이고,
   Linux, FreeBSD, Zircon은 각자의 공식 entry contract를 가진 독립 package다.
8. Windows와 macOS support는 이 ABI에 placeholder를 만들지 않는다. 해당 OS의
   licensing, firmware와 image-chain 계약을 별도 ADR로 동결한 뒤 추가한다.

## Port와 target의 구분

`target`은 architecture, environment, port service와 protocol package를 정적으로
조합하고 image recipe를 정의한다. `port`는 machine wiring만 제공한다. 예를 들어
RPi5 port는 PL011 resource와 허용 payload window를 제공하지만 Parus, Linux 또는
RPH1을 알지 못한다.

QEMU 이름은 harness와 QEMU machine port에만 나타난다. Core, Boot Library, protocol
wire와 kernel receipt에는 QEMU runtime policy가 없다.

## 결과

- 새 ISA는 architecture backend와 해당 OS protocol의 ISA validator만 추가한다.
- 새 board는 필요한 typed port service만 추가한다.
- 새 firmware는 environment adapter만 추가한다.
- 새 OS는 protocol package, image-format allowlist와 entry invocation을 추가한다.
- 동일 boot manager가 configuration 또는 product selection으로 OS protocol을
  교체할 수 있다.

이 ADR은 QEMU execution을 physical hardware, production firmware compatibility 또는
모든 OS runtime support로 승격하지 않는다.

## 기각한 대안

- `RibonPlatformFacts`의 field만 늘리는 방식은 capability와 lifetime을 계속 결합하므로
  기각한다.
- Generic architecture layer에 OS별 flag union을 추가하는 방식은 ISA와 wire protocol
  ownership을 섞으므로 기각한다.
- OS별 top-level boot manager binary를 복제하는 방식은 common boot transaction을
  우회하므로 기각한다.
