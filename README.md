# Ribon

Ribon은 OS profile과 platform adapter를 분리한 deterministic boot and recovery
framework다. Core는 ELF load plan, memory-map normalization, profile dispatch,
handoff buffer ownership만 담당한다. OS wire ABI는 profile이, firmware와 board
protocol은 platform adapter가 담당한다.

## Parus profile

Builtin `parus` profile은 `profiles/parus`에서 Parus Handoff v1(`RPH1`)을 생성한다.
RPH1은 byte-wise little-endian wire format, CRC32C, bounded section table, required
section, borrowed-range 규칙을 사용한다. Producer는 artifact를 반환하기 전에 동일한
bounded parser로 자체 검증한다.

Entry ABI는 모든 64-bit architecture에서 두 argument로 통일한다.

| Architecture | RPH1 pointer | Entry flags |
| --- | --- | --- |
| AMD64 | `rdi` | `rsi` |
| AArch64 | `x0` | `x1` |
| RISC-V 64 | `a0` | `a1` |

Normal mode는 flag `0x1`, optional direct-high bridge는 `0xd`를 전달한다. Higher-half
page-table의 최종 소유권과 runtime mapping policy는 Parus kernel에 있다.

## Build와 검증

```sh
make check
make RIBON_ARCH=x86_64 check-uefi-build
make RIBON_ARCH=aarch64 check-uefi-build
make qstar-check
make docs
```

`make legacy-hard-cut`은 retired OS identifier가 active path 또는 content에 다시
들어오면 실패한다. `.git`, generated `build`, Python cache만 검사 대상에서 제외한다.

Fixture QEMU smoke는 Ribon의 UEFI load와 register handoff를 검증한다. 실제 Parus
kernel QEMU target은 `PARUS_KERNEL_ELF`로 제공한 kernel image를 사용한다.

```sh
make RIBON_ARCH=x86_64 PARUS_KERNEL_ELF=/absolute/path/to/kernel.elf \
  qemu-uefi-parus-smoke
make RIBON_ARCH=aarch64 PARUS_KERNEL_ELF=/absolute/path/to/kernel.elf \
  qemu-uefi-parus-aarch64-smoke
```

QEMU 결과는 VM boot evidence이며 physical hardware 지원 주장이 아니다. Raspberry Pi
package 검증과 live UART 증거는 별도 gate다.

## 문서

`docs/`는 Parus와 같은 Sphinx + MyST Markdown + Breathe 위계를 사용한다.

- `docs/canonical/`: 장기 설계와 ownership
- `docs/contracts/`: wire ABI와 conformance obligation
- `docs/adr/`: 결정과 기각한 대안
- `docs/platforms/`: platform-specific boundary
- `docs/roadmap/`: 의존 순서
- `docs/log/`: 실행 시점의 상태와 증거

문서와 public API 주석 규칙은
[`docs/policy/documentation-policy.md`](docs/policy/documentation-policy.md)를 따른다.
