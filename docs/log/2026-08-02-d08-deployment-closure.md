---
doc_type: devlog
status: accepted
authority: historical
last_verified: 2026-08-02
code_paths:
  - sdk/templates/deployment-consumer/
  - tools/install_sdk.py
  - tools/check_sdk_deployment_consumer.py
  - tools/make_rpi5_prehardware_update.py
  - tools/make_deployment_release_manifest.py
  - tools/check_deployment_release_reproducibility.py
tests:
  - make check-sdk-deployment-consumer
  - make check-rpi5-prehardware
  - make check-deployment-release-reproducibility
  - make check-ribon-deployment-closure
hardware:
  - host
  - qemu-aarch64-virt
  - qemu-x86_64-q35
  - rpi5-not-run
supersedes:
  - none
---

# D08 SDK·release evidence·RPi5 prehardware closure 기록

## 설치 SDK와 외부 소비자

SDK install manifest를 v2로 올리고 Core ABI 5, Plugin ABI 4.0을 정확히 기록했다. Target 표면은
`libribon-core`, `libribon-boot`, `libribon-sdk`, `libribon-update` 네 archive이며 private signer와
concrete crypto provider는 들어가지 않는다. Host-only 표면은 Ribos compiler, verifier, runner,
product composer, update manifest/layout tool과 offline policy signer다.

설치 template에서 임시 out-of-tree recovery/update product를 생성한다. 설치된 product composer,
`ribosc`, independent verifier와 update manifest tool을 실행하고 public header/archive만으로 host
consumer를 link·실행한다. Compiler dependency file을 다시 읽어 source-private dependency가 0임을
검사한다.

## 재현성과 RPi5 prehardware

Host object의 build-root debug path, BSD archive timestamp, report absolute source path와 Darwin host-tool
UUID/signature를 release identity에서 제거하거나 canonicalize했다. 서로 다른 두 clean build root에서
installed SDK, external consumer, RPi5 package, signed prehardware update와 release manifest의 선택된
파일을 exact byte 비교한다.

RPi5 fixture package는 firmware image, ELF payload와 8개 typed module을 10개 canonical update component로
만든다. 각 component는 exact size/hash와 page-aligned bundle offset을 가지며 update manifest는 RFC8032
fixture key의 detached Ed25519 signature를 OpenSSL로 독립 검증한다.

## 증거 경계

- Host build/unit: installed-only product·Ribos·update consumer
- QEMU runtime: D01-D07의 signed update, power-cut recovery, recovery network와 confirmation
- QEMU OS runtime: pinned Linux AArch64 Image가 `/init` unique receipt와 poweroff에 도달
- Package/prehardware: RPi5 raw-FDT module package, signed update set와 clean-root reproducibility
- Not run: physical RPi5 UART, SD durability, power cycle와 live recovery network

이 라운드는 production key custody, HSM/TPM/RPMB, UEFI conformance, fleet rollout, Parus confirmation
integration, Parus user process 또는 다른 architecture의 Linux boot를 주장하지 않는다.
