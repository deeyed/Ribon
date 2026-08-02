---
doc_type: runbook
status: accepted
authority: operational
last_verified: 2026-08-02
code_paths:
  - targets/rpi5-aarch64-raw-fdt/
  - tools/package_rpi5.py
  - tools/check_rpi_package.py
  - tools/make_rpi5_prehardware_update.py
tests:
  - make check-rpi5-prehardware
  - make check-deployment-release-reproducibility
hardware:
  - rpi5-not-run
supersedes:
  - none
---

# RPi5 deployment prehardware와 향후 live 검증 runbook

이 문서는 자동화된 package/prehardware 절차와 아직 수행하지 않은 물리 RPi5 절차를 분리한다.
2026-08-02에는 출장으로 실기기 검증을 실행하지 않았다.

## 자동 prehardware 절차

다음 명령은 SD card나 물리 보드를 사용하지 않는다.

```console
make check-rpi5-prehardware
make check-deployment-release-reproducibility
```

첫 gate는 module-bearing RPi5 raw-FDT product를 만들고 package schema v2의 `kernel8.img`, embedded
payload, product manifest와 8개 typed module range/hash를 검증한다. 이어 10개 component를 page-aligned
`update.bin`에 배치하고 canonical update manifest와 detached Ed25519 envelope를 만든다. 서명 key는
RFC8032 test fixture이므로 production trust claim을 열지 않는다.

결과는 `build/release/rpi5-prehardware/`에 생성되며 `prehardware.json`이 다음 상태를 exact하게
기록한다.

```text
evidence_class = package/prehardware
hardware_execution = not-run
signing_key.class = RFC8032 fixture; non-production
```

두 번째 gate는 별도 clean root 두 곳에서 installed SDK, external consumer, RPi5 package,
prehardware update와 release manifest를 다시 만들고 선택된 모든 파일의 exact byte identity를
비교한다.

## 물리 실행 전 승인 조건

Live 프로그램은 별도 작업으로 시작하고 다음을 먼저 고정한다.

1. 정확한 RPi5 board revision, firmware files와 SD model/capacity
2. UART adapter 전압, baud rate, host device와 capture tool
3. destructive SD target의 안정된 device identity와 operator 확인
4. production이 아닌 live-test key와 rollback domain
5. isolated recovery network, server artifact hash와 address plan
6. cold/warm boot, 반복 횟수, power-cut injection point와 중단 조건

Fixture seed를 production 또는 field device key로 복사하지 않는다. SD 장치가 모호하거나 mounted
filesystem이 있으면 기록을 시작하지 않는다.

## 향후 live 실행 순서

1. clean source revision에서 `check-ribon-deployment-closure`와 prehardware gate를 통과한다.
2. package manifest와 모든 file SHA-256를 별도 evidence directory에 복사한다.
3. 승인된 SD device에 recovery 가능한 방식으로 image를 기록하고 readback hash를 확인한다.
4. 전원 인가 전 UART capture를 시작하고 cold boot receipt와 reset reason을 수집한다.
5. normal boot를 반복해 marker ordering, timeout, watchdog와 unexpected reset 0을 확인한다.
6. inactive slot update를 실행하고 component readback, transaction commit과 pending generation을 확인한다.
7. 정의된 write/flush/metadata point에서 전원을 차단해 confirmed slot 보존과 journal recovery를 확인한다.
8. isolated recovery network에서만 bounded fetch를 실행하고 manifest/envelope/component hash를 대조한다.
9. successful OS health receipt 뒤에만 exact pending generation이 confirmed로 승격되는지 확인한다.
10. 모든 process, serial capture와 network server를 종료하고 raw evidence를 read-only로 보존한다.

## 필수 evidence checklist

- source revision, toolchain version, board revision와 firmware hash
- SD image/write/readback hash와 package manifest
- raw UART log, 시작·종료 시각, 전원 동작과 reset reason
- 각 시험의 expected/observed marker, timeout, exit와 forced cleanup
- update manifest, signature envelope, component와 installed readback hash
- slot journal before/after, pending/confirmed generation과 rollback counter
- recovery server object hash와 bounded request log
- negative/failure run도 삭제하지 않은 raw record

물리 성공 claim은 이 checklist를 한 실행에서 닫고 raw UART와 structured result를 함께 보존한 뒤에만
열 수 있다. QEMU, package, prehardware 결과는 이를 대신하지 않는다.
