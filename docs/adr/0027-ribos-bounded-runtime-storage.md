---
doc_type: adr
status: accepted
authority: normative
last_verified: 2026-07-31
code_paths:
  - language/ribos/base/include/ribos/base/checked.h
  - language/ribos/vm/include/ribos/vm/storage.h
  - language/ribos/vm/src/runtime/storage.c
  - docs/contracts/language/ribos-runtime-storage-v1.md
tests:
  - make check-ribos-runtime-storage
  - make check-ribos-prepared-program
  - make check-ribos-host-boundary
  - make check
  - make docs
hardware:
  - none
supersedes:
  - implicit interpreter heap and native frame stack
---

# ADR 0027: Runtime을 caller-owned fixed-offset arena로 제한

## Context

PreparedProgram이 artifact와 verifier 결과를 봉인해도 interpreter가 frame, aggregate,
handle 또는 output을 필요할 때 heap을 사용하면 resource closure가 실제 target memory
상한을 설명하지 못한다. Native C structure와 union을 arena에 그대로 배치하면 pointer
폭, padding, alignment와 endian에 따라 required byte와 Ribos value 의미가 달라진다.

Artifact의 maximum stack만 arena size로 사용하면 loop/helper counter, handle table,
aggregate scratch, terminal outcome, fault와 trace가 hidden storage가 된다. 반대로
product maximum arena 전체를 무조건 소비하면 artifact마다 exact required byte를
질의할 수 없고 작은 normal product가 불필요하게 큰 storage를 요구한다.

## Decision

- `ribos_vm_runtime_size_v1`이 PreparedProgram에서 exact fixed-offset plan을 계산한다.
- Runtime은 initialization 뒤 하나의 caller-owned 8-byte-aligned arena만 사용한다.
- Layout offset과 byte 산술은 `uint64_t`, caller buffer 변환은 checked `size_t`를 쓴다.
- Effective arena limit은 generic 128 MiB absolute cap과 product/mode cap의 교집합이다.
- Verified stack/depth/slot/loop/helper 수와 product handle/output/trace cap을 함께
  layout에 포함한다.
- Control, frame, slot state, counter, handle, aggregate, outcome, output, fault와 trace
  region 순서를 storage ABI 1.0으로 고정한다.
- Arena 내부 descriptor와 scalar value는 explicit little-endian byte access를 쓴다.
- C union, packed cast, raw wire row cast와 native pointer를 runtime value storage로
  사용하지 않는다.
- Function-owned typed slot은 exact verified frame offset과 type size로만 접근한다.
- Optional diagnostic poison은 state validation을 보조하지만 correctness 경계는
  uninitialized/initialized/moved state machine이 소유한다.
- Product-specific byte 상수는 VM source에 넣지 않고 Prepared effective limit으로
  주입한다.

## Consequences

- Caller는 runtime을 시작하기 전에 필요한 byte를 정확히 알 수 있다.
- Arena가 한 byte 작거나 misaligned여도 partial runtime state를 만들지 않는다.
- Interpreter의 최대 dynamic storage를 heap 또는 hidden C recursion 없이 설명할 수
  있다.
- 32/64-bit host와 target이 같은 artifact/limit에 대해 같은 logical region layout을
  계산한다.
- Handle, outcome와 trace record는 후속 라운드가 채울 bounded reservation을 가지지만
  이번 결정만으로 그 semantic dispatch가 구현되지는 않는다.
- Maximum output, handle와 trace cap을 크게 잡으면 exact required arena도 커진다.
  Product composer가 mode별 현실적인 cap을 선택해야 한다.
- Public typed slot API는 runtime 내부 기반을 노출하지만 raw MMIO, native pointer 또는
  artifact wire cast를 정책 값으로 노출하지 않는다.

## 기각한 대안

### Interpreter 내부 heap

Worst-case allocation과 allocation failure 시점을 resource closure에서 설명할 수 없고
normal pre-OS product에 allocator authority를 추가하므로 기각한다.

### Native C frame와 value union

Host pointer width, compiler padding와 alignment가 language value ABI와 runtime size를
바꾸므로 기각한다.

### Verifier maximum stack만 allocation

Counter, handle, aggregate, outcome, output, fault와 trace가 hidden memory가 되므로
기각한다.

### Product maximum arena 전체를 항상 zero/init

Artifact별 exact query를 잃고 작은 policy도 가장 큰 product capacity를 전부
초기화하므로 기각한다.

### Artifact가 runtime arena cap을 단독 결정

Signed artifact가 product/mode memory policy를 넓힐 수 있으므로 기각한다. Artifact
closure는 필요한 하한을 제공하고 product descriptor가 허용 상한을 제공한다.

### Artifact wire reader를 runtime value accessor로 재사용

Wire table와 mutable execution value의 lifetime, canonical validation과 ownership이
다르므로 별도 little-endian accessor를 유지한다.
