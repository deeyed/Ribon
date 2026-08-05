# Ribon Make modules

The root `Makefile` owns only module order. Public targets remain stable while
their implementation is separated by authority:

- `config.mk`: caller-overridable tools, flags, build roots and architecture.
- `model.mk`: source lists, product paths and generated artifact identities.
- `rules/tooling.mk`: help, dependency doctor, CI and build-system self-tests.
- `rules/core.mk`: common libraries, host reference and shared object rules.
- `rules/ribos.mk`: Ribos compiler, verifier, VM and cross-architecture gates.
- `rules/security-update.mk`: signature, key policy, update and recovery gates.
- `rules/raw-fdt.mk`: AArch64/RISC-V raw-FDT and RPi5 products.
- `rules/uefi-bios.mk`: UEFI, FreeBSD/Linux EFI and BIOS products.
- `rules/host-sdk.mk`: public API, SDK, firmware provider and graph gates.
- `rules/aggregate.mk`: aggregate checks, documentation and cleanup.

Product and security meaning remains source-owned by JSON manifests and the
QStar composition graph. These modules orchestrate those inputs; they do not
define a second product schema.
