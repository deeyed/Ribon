---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-08-02
code_paths:
  - include/Ribon/update/confirmation.h
  - src/update/confirmation.c
  - include/Ribon/protocol/protocol.h
  - products/validation/uefi-update-recovery/
  - targets/x86_64-uefi-update-recovery/entry.c
  - tools/qemu_boot_confirmation.py
tests:
  - make check-boot-confirmation
  - qstar --file qstar.lua test --suite //tests:boot_confirmation_tests
hardware:
  - qemu-q35-uefi
  - rpi5-not-run
supersedes:
  - none
---

# D06 OS-neutral boot confirmation 실행 기록

## 구현 결과

Explicit little-endian signed envelope, exact boot-attempt identity, dedicated key-policy usage와
protocol-owned health callback을 추가했다. Protected-state wire는 160 byte로 확장해 binding digest와
domain-monotonic attempt sequence를 보존한다. Update transaction은 exact pending을 confirmed active
slot로 바꾸며 동일 identity 재확인은 write 없는 success다.

Validation product는 update disk trailing reserved region의 네 개 512-byte page에 reference protected
journal을 저장한다. 이 adapter는 provider class를 `REFERENCE`로 선언한다. Core source와 ABI에는 OS
이름, service marker, UEFI handle, filesystem path와 raw storage address가 없다.

## 검증 evidence

- Host/unit: seven hostile classes, timeout 뒤 새 attempt, stale receipt replay, healthy confirmation과
  exact duplicate
- Sanitizer: 같은 corpus의 ASAN/UBSAN 실행
- Cross compile: x86_64, AArch64, RISC-V freestanding confirmation object
- Product graph: confirmation 제품만 dedicated key usage를 선택하고 network-only 제품은 선택하지 않음
- QEMU runtime: 동일 q35 UEFI disk를 세 번 재사용해 pending attempt, confirmed receipt, duplicate reopen
- Cleanup: 세 boot 모두 process-group cleanup complete, forced kill 0

Raw serial, inspector JSON, result JSON, disk/firmware/application/manifest/confirmation hash는
`build/targets/x86_64-uefi-update-recovery/results/boot-confirmation/`에 남는다. Build output은 source
control에 commit하지 않는다.

## OS companion handoff

Parus 통합 작업에는 새 Ribon wire protocol이 필요하지 않다. Parus companion은 boot가 충분히 healthy인
시점에 protocol-owned payload를 생성하고, product/slot/image/manifest/policy/nonce/attempt identity가
그대로 들어간 signed envelope를 다음 reboot에서 Ribon이 읽을 수 있는 platform-owned persistent
mailbox에 기록해야 한다. Ribon의 Parus protocol callback은 그 payload codec이 승인되고 negative
corpus가 생길 때까지 `UNSUPPORTED`다. Core에 Parus marker나 fallback을 추가해서는 안 된다.

## 비주장

현재 QEMU target은 validation receipt를 firmware 안에서 재현한다. 새 pending OS image로 실제 제어를
넘기거나 Parus가 receipt를 생산한 증거가 아니다. 출장 중이므로 RPi5 실기기를 실행하지 않았다.
Production nonce entropy, hardware anti-replay, physical power-loss durability, UEFI Secure Boot와
production key provisioning도 이 라운드의 claim이 아니다.
