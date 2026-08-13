.PHONY: help doctor doctor-host doctor-targets doctor-qemu doctor-docs \
	check-build-system ci-host ci-targets ci-qemu ci-docs

BUILD_DOCTOR := $(ROOT)/tools/make/doctor.py
BUILD_TOOL_TESTS := $(ROOT)/tests/make/build_tool_tests.py
MAKE_MODULE_LINT := $(ROOT)/tools/lint/make_module_lint.py

help:
	@echo "Ribon build entry points:"
	@echo "  make all                 host libraries and reference product"
	@echo "  make check               aggregate host, cross-compile and QStar gates"
	@echo "  make check-target-builds BIOS, RPi5, raw-FDT and UEFI product builds"
	@echo "  make ci-qemu             architecture fixture smokes under QEMU"
	@echo "  make sdk-install         install the public SDK under BUILD_ROOT"
	@echo "  make docs                Doxygen, Breathe and Sphinx documentation"
	@echo "  make doctor              validate every supported local build lane"
	@echo "Override tools with CC=, CROSS_CC=, QSTAR=, QEMU_X86_64=, etc."

doctor-host:
	$(PYTHON) $(BUILD_DOCTOR) --scope host \
		--tool CC=$(CC) --tool AR=$(AR) --tool PYTHON=$(PYTHON) \
		--tool QSTAR=$(QSTAR) --tool OPENSSL=$(OPENSSL)

doctor-targets:
	$(PYTHON) $(BUILD_DOCTOR) --scope targets \
		--tool X86_64_CC=$(X86_64_CC) \
		--tool AARCH64_CC=$(AARCH64_CC) \
		--tool RISCV64_CC=$(RISCV64_CC) \
		--tool LD_LLD=$(LD_LLD) --tool LLD_LINK=$(LLD_LINK) \
		--tool OBJCOPY=$(OBJCOPY) --tool LLVM_AR=$(LLVM_AR)

doctor-qemu: doctor-targets
	$(PYTHON) $(BUILD_DOCTOR) --scope qemu \
		--tool QEMU_AARCH64=$(QEMU_AARCH64) \
		--tool QEMU_X86_64=$(QEMU_X86_64) \
		--tool QEMU_RISCV64=$(QEMU_RISCV64) \
		--file X86_64_UEFI_FIRMWARE=$(X86_64_UEFI_FIRMWARE) \
		--file RISCV64_OPENSBI_FIRMWARE=$(RISCV64_OPENSBI_FIRMWARE)

doctor-docs:
	$(PYTHON) $(BUILD_DOCTOR) --scope docs \
		--tool DOXYGEN=$(DOXYGEN) --tool SPHINX_BUILD=$(SPHINX_BUILD)

doctor: doctor-host doctor-targets doctor-qemu doctor-docs

check-build-system:
	$(PYTHON) $(MAKE_MODULE_LINT) --root $(ROOT)
	$(PYTHON) $(BUILD_TOOL_TESTS) \
		--doctor $(BUILD_DOCTOR) --firmware-resolver $(FIRMWARE_RESOLVER) \
		--llvm-tool-resolver $(LLVM_TOOL_RESOLVER)

ci-host: doctor-host doctor-qemu check

ci-targets: doctor-targets check-target-builds

ci-qemu: doctor-qemu qemu-aarch64-virt-modules-fixture-smoke \
	qemu-riscv64-virt-rlh1-fixture-smoke x86_64-uefi-parus-fixture-smoke

ci-docs: doctor-docs docs
