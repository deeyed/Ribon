check-one: $(HOST_REFERENCE) $(KERNEL_FIXTURE)
	$(HOST_REFERENCE) --kernel $(KERNEL_FIXTURE) > $(BUILD_DIR)/host-reference.txt
	grep -q "product=host-reference" $(BUILD_DIR)/host-reference.txt
	grep -q "registry-plugins=5" $(BUILD_DIR)/host-reference.txt
	grep -q "core-library=libribon-core" $(BUILD_DIR)/host-reference.txt
	grep -q "boot-library=libribon-boot" $(BUILD_DIR)/host-reference.txt
	grep -q "environment=host" $(BUILD_DIR)/host-reference.txt
	grep -q "arch=$(RIBON_ARCH)" $(BUILD_DIR)/host-reference.txt
	grep -q "protocol=synthetic" $(BUILD_DIR)/host-reference.txt
	grep -q "image-format=elf64" $(BUILD_DIR)/host-reference.txt
	grep -q "normalized-memory-regions=5" $(BUILD_DIR)/host-reference.txt
	grep -q "handoff-format=synthetic-v1" $(BUILD_DIR)/host-reference.txt
	grep -q "lifecycle-stage=8" $(BUILD_DIR)/host-reference.txt
	@echo "RIBON-R4-HOST-REFERENCE-OK $(RIBON_ARCH)"

check-target-builds: bios-compile rpi5-aarch64-raw-fdt-package \
	rpi5-aarch64-modules-fixture-package \
	qemu-aarch64-virt-raw-fdt qemu-aarch64-virt-modules-fixture-product \
	qemu-aarch64-virt-linux-product \
	qemu-riscv64-virt-rph1-fixture-product \
	qemu-riscv64-virt-linux-product x86_64-uefi-parus-fixture \
	x86_64-uefi-linux-product x86_64-uefi-freebsd-product \
	x86_64-uefi-network-update-recovery
	$(PYTHON) tools/lint/target_object_graph_lint.py $(TARGET_BUILD_ROOT)

check-uefi-product-hermeticity: $(UEFI_LINUX_EXTERNAL_STAMP) \
		$(UEFI_FREEBSD_EXTERNAL_STAMP)
	$(PYTHON) tools/check_uefi_product_hermeticity.py \
		--make $(MAKE) --root $(ROOT) \
		--work-root $(BUILD_ROOT)/tests/uefi-product-hermeticity \
		--linux-cache $(UEFI_LINUX_CACHE) \
		--freebsd-compressed-cache $(UEFI_FREEBSD_COMPRESSED_CACHE) \
		--freebsd-raw-cache $(UEFI_FREEBSD_RAW_CACHE)

check: check-build-system legacy-hard-cut check-public-api check-frontends check-loader \
	check-ribos-parser-pilot check-ribos-semantics check-ribos-schema \
	check-ribos-ir check-ribos-resources check-ribos-artifact \
	check-ribos-verifier check-ribos-runtime-contract \
	check-ribos-prepared-program check-ribos-runtime-storage \
	check-ribos-vm-scalar check-ribos-vm-calls \
	check-ribos-vm-loops check-ribos-vm-aggregates \
	check-ribos-vm-handles check-ribos-vm-helpers \
	check-ribos-vm-terminal check-ribos-vm-faults \
	check-ribos-executable-corpus check-ribos-vm \
	check-ribos-ribon-integration check-ribos-product-graphs \
	check-ribos-normal-no-network check-ribos-factory-recovery \
	check-ribos-host-boundary check-ribos-r18 \
	check-ribos-production-policy \
	check-security-ed25519-provider check-security-ed25519-sanitizer \
	check-security-ed25519-cross-compile check-security-provider-graphs \
	check-security-key-policy check-security-key-policy-sanitizer \
	check-security-key-policy-graphs \
	check-security-protected-state check-security-protected-state-sanitizer \
	check-security-protected-state-graphs \
	check-update-manifest check-update-manifest-sanitizer \
	check-update-manifest-cross-compile \
	check-update-storage check-update-storage-sanitizer \
	check-update-storage-cross-compile check-update-storage-graphs \
	check-update-installer check-uefi-update-storage \
	check-update-power-cut check-boot-confirmation check-recovery-network-update \
	check-pe-coff check-fdt check-rph1 check-arch-x86_64 \
	check-arch-aarch64 check-arch-ops \
	check-core-service check-port-services check-boot-lifecycle \
	check-environment-persistent-inputs check-boot-modules check-media-pipeline check-mode-descriptors check-plugin-descriptors \
	check-protocol-contract check-parus-entry-contract check-os-packages \
	check-linux-boot check-linux-external-input \
	check-library-embed check-composition-schemas \
	check-qemu-evidence check-uefi-product-hermeticity \
	check-sdk-surface check-sdk-embed check-sdk-reproducible \
	check-external-plugin check-ribos-extension-sdk check-firmware-personalities \
	check-firmware-object-graphs check-object-graphs check-normal-media-surface qstar-check
	@for arch in $(RIBON_ARCHES); do \
		$(MAKE) --no-print-directory RIBON_ARCH=$$arch check-one || exit $$?; \
	done
	@echo "RIBON-R5-AGGREGATE-OK"

docs: legacy-hard-cut
	$(PYTHON) tools/lint/documentation_quality_lint.py
	@mkdir -p $(BUILD_ROOT)/docs
	RIBON_BUILD_ROOT=$(BUILD_ROOT) $(DOXYGEN) docs/Doxyfile
	RIBON_BUILD_ROOT=$(BUILD_ROOT) $(SPHINX_BUILD) -W --keep-going \
		-b html docs $(BUILD_ROOT)/docs/html

docs-lint: legacy-hard-cut
	$(PYTHON) tools/lint/documentation_quality_lint.py

docs-clean:
	rm -rf $(BUILD_ROOT)/docs

clean:
	rm -rf $(BUILD_ROOT)
