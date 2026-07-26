---
doc_type: canonical
status: accepted
authority: normative
last_verified: 2026-07-26
code_paths:
  - docs/
  - include/Ribon/
  - src/
tests:
  - ribon-documentation-quality-lint
  - ribon-docs
hardware:
  - none
supersedes:
  - flat Ribon documentation tree
---

# Ribon 문서

Ribon 문서는 설계 정본, 코드 계약, 결정 기록, 개발 순서, 구현 증거를 서로 다른
권위로 분리한다. 일반 문서는 MyST Markdown으로 작성하고 공개 C API는 Doxygen XML과
Breathe를 통해 같은 Sphinx 문서에 포함한다.

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
:caption: 계약

contracts/core/core-profile-platform-boundary
contracts/handoff/parus-handoff-v1
contracts/entry/higher-half-ownership
contracts/update/ota-update-authority
contracts/recovery/overseer-watchdog-model
contracts/network/recovery-network-surface
contracts/platform/platform-support-tiers
contracts/documentation/documentation-quality-gate
```

```{toctree}
:maxdepth: 2
:caption: 결정 기록

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
:caption: 개발 순서

roadmap/ribon-development-program
```

```{toctree}
:maxdepth: 2
:caption: 참고 자료

references/update-and-platform-standards
api/public-c-api
```

```{toctree}
:maxdepth: 1
:caption: 개발 기록

log/2026-07-26-r0-documentation-hard-cut
log/2026-07-26-r1-parus-profile-and-rph1
log/2026-07-26-r2-core-service-boundary
```
