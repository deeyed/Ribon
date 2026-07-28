---
doc_type: canonical
status: accepted
authority: normative
last_verified: 2026-07-27
code_paths:
  - docs/
  - include/Ribon/
  - src/
  - products/
  - targets/
tests:
  - ribon-documentation-quality-lint
  - ribon-docs
hardware:
  - none
supersedes:
  - profile-centered Ribon documentation index
---

# Ribon 문서

Ribon은 generic boot runtime library, multiprotocol boot manager, firmware/plugin SDK다.
문서는 상태 없는 정본, ABI 계약, 결정 기록, 개발 순서, 실행 증거를 서로 다른 권위로
분리한다.

일반 문서는 MyST Markdown으로 작성하고 public C API는 Doxygen XML과 Breathe를 통해
Sphinx 문서에 통합한다.

```{toctree}
:maxdepth: 2
:caption: 정책

policy/documentation-policy
```

```{toctree}
:maxdepth: 2
:caption: 정본 설계

canonical/architecture/ribon-architecture
canonical/lifecycle/boot-update-recovery-model
```

```{toctree}
:maxdepth: 2
:caption: Library와 Plugin 계약

contracts/core/library-plugin-protocol-boundary
contracts/composition/product-plugin-composition
contracts/frontends/environment-port-image-boundary
contracts/storage/deterministic-boot-media
contracts/firmware/firmware-personality-plugin
contracts/firmware/reference-provider-products
contracts/sdk/sdk-install-and-package
api/public-c-api
```

```{toctree}
:maxdepth: 2
:caption: Boot와 복구 계약

contracts/handoff/parus-handoff-v1
contracts/boot/generic-entry-and-port-services
contracts/protocols/os-package-support-matrix
contracts/entry/os-entry-ownership
contracts/update/update-authority
contracts/recovery/overseer-plugin-boundary
contracts/network/recovery-network-surface
contracts/platform/target-support-tiers
contracts/evidence/qemu-payload-evidence
contracts/documentation/documentation-quality-gate
```

```{toctree}
:maxdepth: 2
:caption: Active 결정

adr/0009-limine-library-plugin-hard-cut
adr/0010-typed-service-graph-hard-cut
adr/0011-bounded-boot-transaction-hard-cut
adr/0012-deterministic-boot-media-pipeline
adr/0013-generic-entry-port-protocol-hard-cut
```

```{toctree}
:maxdepth: 2
:caption: Superseded 결정 기록

adr/0001-legacy-os-semantic-hard-cut
adr/0002-core-profile-platform-boundary
adr/0003-parus-handoff-v1
adr/0004-kernel-owned-higher-half
adr/0005-split-ota-authority
adr/0006-reboot-time-overseer
adr/0007-bounded-recovery-network
adr/0008-bios-riscv-support-tiers
```

```{toctree}
:maxdepth: 2
:caption: 플랫폼

platforms/rpi5/native-boot-boundary
```

```{toctree}
:maxdepth: 2
:caption: 개발 프로그램

roadmap/ribon-development-program
```

```{toctree}
:maxdepth: 2
:caption: 참고 자료

references/update-and-platform-standards
```

```{toctree}
:maxdepth: 1
:caption: 역사적 개발 기록

log/2026-07-26-r0-documentation-hard-cut
log/2026-07-26-r1-parus-profile-and-rph1
log/2026-07-26-r2-core-service-boundary
log/2026-07-26-r3-library-plugin-protocol-hard-cut
log/2026-07-26-r4-environment-protocol-targets
log/2026-07-26-r5-sdk-firmware-composition
log/2026-07-27-r6-typed-service-graph
log/2026-07-27-r7-bounded-boot-lifecycle
log/2026-07-27-r8-deterministic-boot-media
```
