---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-07-31
code_paths:
  - Makefile
  - targets/targets.qst
  - products/bootmgr/manifests/x86_64-uefi-parus-fixture.json
  - products/bootmgr/manifests/x86_64-uefi-parus-external.json
  - tools/check_uefi_product_hermeticity.py
  - tools/validate_external_parus_payload.py
tests:
  - make check-uefi-product-hermeticity
  - make x86_64-uefi-parus-fixture-smoke
  - make x86_64-uefi-parus-external-smoke
  - make check-target-builds
  - make qstar-check
  - make check
  - make docs
hardware:
  - not-run
supersedes:
  - none
---

# R01 UEFI product hermetic build 구현 기록

## 구현

R01은 x86_64 UEFI fixture와 external-Parus consumer를 서로 다른 product identity와
output root로 분리했다. 두 product는 application, generated registry, object, linker map,
ESP, copied manifest와 machine-readable result를 공유하지 않는다. 이전의 환경 변수 기반
shared `x86_64-uefi-app` 선택 경로와 compatibility alias는 제거했다.

Fixture product는 Ribon이 생성하는 ABI fixture만 소비한다. External product는 명시적으로
제공된 payload를 매 호출마다 다시 검증하고 product-local ESP로 복사한다. 검증은 manifest의
architecture와 entry ABI, ELF load window, payload digest 및 fixture marker 부재를 확인한다.
따라서 fixture payload는 external-kernel product의 입력으로 사용할 수 없다.

PE/COFF link에는 reproducible build option을 적용했다. Hermeticity gate는 두 개의 독립
build root에서 fixture와 서로 다른 external input의 빌드 순서를 바꾸어 실행하고, canonical
output identity, manifest provenance, product-local payload와 writable path disjointness를
검사한다.

## 검증 결과

- hermeticity gate:
  `RIBON-UEFI-PRODUCT-HERMETICITY-OK products=2 orders=2 roots=2 shared-outputs=0`
- fixture QEMU product:
  `bootmgr.x86_64-uefi-parus-fixture`
- fixture payload SHA-256:
  `42dfb3ae553d0eaa122e38807c40fe22c75a0fce3b25e797674839596857ff95`
- actual-kernel QEMU product:
  `bootmgr.x86_64-uefi-parus-external`
- actual Parus payload SHA-256:
  `26b2edb1fabc9d748c7699bcb9d4bf761cf14213f6abe45a7473f6b8590bc9b5`
- fixture와 actual-kernel QEMU 결과: 모두 `outcome=passed`
- 두 QEMU process group: 정상 종료, forced kill 없음, 잔존 process 없음
- aggregate `make check`: 성공

External smoke는 Parus source revision
`96c64d77db9c7a07f3a72943472b4980004be729`에서 생성된 AMD64 RPH1 ELF를 읽기 전용으로
소비했다. Ribon은 해당 payload를 빌드하거나 수정하지 않았으며, QEMU에서 Ribon UEFI
transfer 뒤 Parus `IDLE:OK` terminal receipt까지 관찰했다.

## 증거 경계

Hermeticity gate가 만드는 synthetic ELF는 build-order 및 output isolation 시험 입력일 뿐
운영체제 boot 증거가 아니다. Actual-kernel smoke는 현재 AMD64 QEMU와 EDK2 환경의
guest-executed evidence이며 physical hardware, production key, Secure Boot enrollment,
rollback counter, OTA 또는 UEFI conformance 증거가 아니다. RPi5 검증은 이 라운드에서도
package-only이고 실기기 UART를 실행하지 않았다.
