---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-08-01
code_paths:
  - include/Ribon/update/transaction.h
  - src/update/transaction.c
  - src/update/installer.c
  - tests/update/power_cut_tests.c
  - targets/x86_64-uefi-update-recovery/entry.c
  - tools/inspect_qemu_update_transaction.py
  - tools/qemu_update_power_cut.py
tests:
  - make check-update-power-cut
  - make check-qemu-update-install
  - make check-update-storage-graphs
  - qstar --file qstar.lua check
hardware:
  - qemu-q35-uefi
  - rpi5-not-run
supersedes:
  - none
---

# D04 transactional update fault closure 실행 기록

## 구현 결과

- Generic transaction coordinator와 explicit LE record/selector journal v1을 추가했다.
- Signed installer는 same-identity `STAGING`, `VERIFIED`, `PENDING` resume를 지원하고 stable operation
  boundary를 observer에 제공한다.
- Durable 순서를 STAGING commit, payload exact write/full readback/flush, VERIFIED commit, PENDING commit으로
  고정했다.
- Scanner는 stale selector downgrade, same-generation selector conflict, corrupt newest record fallback과
  external floor 아래 replay를 fail-closed한다.
- Recovery UEFI product에만 transaction source를 link했으며 normal UEFI source graph는 변하지 않았다.
- Deterministic fixture가 generation 1 transaction journal을 포함하고 independent Python inspector가
  GPT, anchor, layout, journal과 payload를 함께 검증한다.

## 실행 evidence

Reference crash model은 clean two-component transaction의 54개 before/after event 전체에서 fail-stop을
주입했다. 모든 case에서 active slot A의 confirmed byte가 유지됐고 대상 B가 partial confirmed가 되지
않았다. Clean retry는 한 번의 `PENDING` generation 4로 수렴했고 두 번째 retry는 generation을
증가시키지 않았다.

Hostile corpus는 final component short write, payload flush failure, readback mutation, stale selector,
same-generation selector conflict, external-floor whole-media replay, torn newest record와 torn inactive
selector를 검사했다. ASAN/UBSAN과 x86_64, AArch64, RISC-V freestanding compile gate도 같은 coordinator를
검사했다.

QEMU 11.0.2 q35 TCG와 OVMF에서는 host model이 만든 STAGING commit 직후, payload flush 직후,
VERIFIED commit 직후의 3개 disk를 각각 recovery/reopen하여 총 6회 부팅했다. 모든 결과는
`B=PENDING`, generation 4, attempts 3으로 닫혔고 active A digest는 유지됐다. Network는 비활성화했고
forced kill 없이 process group cleanup을 완료했다. OVMF가 생성한 `NvVars`는 firmware-owned mutable
output으로 별도 hash를 기록했다.

## 비주장

출장으로 RPi5 실기기를 실행하지 않았다. Host flush model은 실제 controller cache, 전원 차단과
거짓 flush 성공을 재현하지 않는다. 이번 결과는 physical flash durability/wear, hardware anti-replay,
network OTA, protected-state trial 결합, OS confirmation, pending image의 실제 boot 또는 production
Secure Boot 성공을 입증하지 않는다.
