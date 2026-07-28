---
doc_type: contract
status: accepted
authority: normative
last_verified: 2026-07-28
code_paths:
  - tools/qemu_target_smoke.py
  - tests/tools/qemu_target_smoke_tests.py
tests:
  - make check-qemu-evidence
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

External-kernel class는 actual payload identity와 Ribon transfer 증거를 연다. Parus
boot stage 또는 runtime 성공은 별도 required marker graph와 Parus integration
harness가 검증해야 하며, payload class만으로 열리지 않는다.

## Result와 cleanup

Result는 다음 authority를 분리해 보존한다.

- expected product class와 observed payload class
- Ribon source revision
- payload와 composed artifact SHA-256
- firmware SHA-256
- QEMU version과 실제 command
- bounded timeout과 terminal reason
- process-group cleanup, forced kill, stale process group
- raw serial path/hash
- required marker count/order와 first divergence

Preflight rejection처럼 QEMU를 launch하지 않은 결과도 `cleanup` record를 가진다.
성공은 required marker가 정확히 한 번 순서대로 관측되고, payload가 immutable하며,
cleanup complete와 forced kill false일 때만 성립한다.

## Claim 경계

Ribon QEMU evidence success는 선택된 QEMU target과 payload identity에 한정된다.
Physical hardware, production firmware, Parus full boot, VM, SMP 또는 user runtime
성공을 대신하지 않는다.
