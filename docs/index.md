---
doc_type: canonical
status: accepted
authority: normative
last_verified: 2026-07-31
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
Ribos는 그 위에서 bounded boot, board adaptation, update와 recovery policy를 표현하는
정적 타입 source language다.
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
canonical/language/ribos-language-model
```

```{toctree}
:maxdepth: 2
:caption: Library와 Plugin 계약

contracts/core/library-plugin-protocol-boundary
contracts/composition/product-plugin-composition
contracts/security/signed-object-trust-v1
contracts/security/signature-provider-v1
contracts/security/key-policy-rollback-v1
contracts/security/protected-state-journal-v1
contracts/frontends/environment-port-image-boundary
contracts/frontends/raw-fdt-boot-module-bundle
contracts/storage/deterministic-boot-media
contracts/firmware/firmware-personality-plugin
contracts/firmware/reference-provider-products
contracts/sdk/sdk-install-and-package
contracts/language/ribos-source-language
contracts/language/ribos-policy-ir-v1
contracts/language/ribos-resource-closure-v1
contracts/language/ribos-bytecode-artifact-v1
contracts/language/ribos-bytecode-verifier-v1
contracts/language/ribos-host-target-boundary
contracts/language/ribos-vm-runtime-v1
contracts/language/ribos-prepared-program-v1
contracts/language/ribos-runtime-storage-v1
contracts/language/ribos-scalar-interpreter-v1
contracts/language/ribos-bounded-calls-loops-v1
contracts/language/ribos-bounded-aggregate-runtime-v1
contracts/language/ribos-generation-handles-v1
contracts/language/ribos-typed-helper-dispatch-v1
contracts/language/ribos-terminal-outcome-recovery-v1
contracts/language/ribos-host-replay-v1
contracts/language/ribos-executable-corpus-v1
contracts/language/ribos-ribon-product-integration-v1
contracts/language/ribos-cross-architecture-validation-v1
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
adr/0014-riscv64-bootstrap-hart-authority
adr/0015-ribos-bounded-policy-language
adr/0016-tracked-ribos-pegen-snapshot
adr/0018-ribos-bounded-typed-front-end
adr/0019-ribos-policy-ir-and-product-schema
adr/0020-ribos-cfg-resource-closure
adr/0021-ribos-bytecode-artifact
adr/0022-ribos-independent-bytecode-verifier
adr/0023-ribos-stage2-policy-verifier
adr/0024-ribos-host-target-boundary
adr/0025-ribos-vm-runtime-contract
adr/0026-ribos-authorized-prepared-program
adr/0027-ribos-bounded-runtime-storage
adr/0028-ribos-portable-switch-interpreter
adr/0029-ribos-explicit-frame-and-loop-accounting
adr/0030-ribos-inline-bounded-aggregate-execution
adr/0031-ribos-generation-handle-ownership
adr/0032-ribos-typed-helper-dispatch
adr/0033-ribos-terminal-action-fault-recovery
adr/0034-ribos-production-vm-host-replay
adr/0035-ribos-ribon-product-integration
adr/0036-ribos-cross-architecture-qemu-validation
adr/0037-uefi-product-scoped-hermetic-build
adr/0038-ribos-executable-example-corpus
adr/0039-raw-fdt-typed-module-bundle
adr/0040-product-bound-policy-trust
adr/0041-freestanding-ed25519-provider
adr/0042-product-bound-key-policy-engine
adr/0043-protected-rollback-state-journal
adr/0044-signed-ribos-authorization-pipeline
adr/0045-signed-ribos-release-evidence
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
adr/0017-ribos-language-project-hierarchy
```

```{toctree}
:maxdepth: 2
:caption: 플랫폼

platforms/rpi5/native-boot-boundary
platforms/ribos-signed-policy-operations
```

```{toctree}
:maxdepth: 2
:caption: 개발 프로그램

roadmap/ribon-development-program
roadmap/ribos-compiler-bootstrap
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
log/2026-07-29-w3-riscv64-rph1-fixture
log/2026-07-30-ribos-parser-pilot
log/2026-07-30-ribos-project-hierarchy
log/2026-07-30-ribos-typed-front-end
log/2026-07-30-ribos-policy-ir-v1
log/2026-07-31-ribos-cfg-resource-closure
log/2026-07-31-ribos-bytecode-artifact
log/2026-07-31-ribos-bytecode-verifier-stage1
log/2026-07-31-ribos-bytecode-verifier-stage2
log/2026-07-31-ribos-host-target-boundary
log/2026-07-31-ribos-host-replay-hardening
log/2026-07-31-ribos-ribon-product-integration
log/2026-07-31-ribos-cross-architecture-vm-validation
log/2026-07-31-r01-uefi-product-hermetic-build
log/2026-07-31-r02-ribos-executable-corpus
log/2026-07-31-rfdt-mod0-typed-boot-modules
log/2026-07-31-r03-production-trust-contracts
log/2026-07-31-r04-freestanding-ed25519
log/2026-08-01-r05-product-bound-key-policy
log/2026-08-01-r06-protected-rollback-state
log/2026-08-01-r07-signed-ribos-authorization
log/2026-08-01-r08-signed-policy-release-evidence
```
