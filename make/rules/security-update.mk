check-security-ed25519-provider: $(SECURITY_TEST)
	$(SECURITY_TEST)
	$(PYTHON) tests/security/ed25519_cross_tool_tests.py \
		--openssl openssl \
		--message-vector tests/fixtures/security/ribos-policy-trust-v1.json \
		--signature-vector \
			tests/fixtures/security/ribos-policy-trust-ed25519-v1.json \
		--seed tests/fixtures/security/rfc8032-test1-seed.hex

check-security-ed25519-sanitizer: $(SECURITY_SANITIZER_TEST)
	$(SECURITY_SANITIZER_TEST)

$(KEY_POLICY_TEST): tests/security/key_policy_tests.c \
		$(SECURITY_KEY_POLICY_SRCS) $(SECURITY_PROVIDER_SRCS) \
		include/Ribon/security/key_policy.h $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(SECURITY_INCLUDE_FLAGS) \
		tests/security/key_policy_tests.c $(SECURITY_KEY_POLICY_SRCS) \
		$(SECURITY_PROVIDER_SRCS) -o $@

$(KEY_POLICY_SANITIZER_TEST): tests/security/key_policy_tests.c \
		$(SECURITY_KEY_POLICY_SRCS) $(SECURITY_PROVIDER_SRCS) \
		include/Ribon/security/key_policy.h $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g $(WARNFLAGS) \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$(SECURITY_INCLUDE_FLAGS) tests/security/key_policy_tests.c \
		$(SECURITY_KEY_POLICY_SRCS) $(SECURITY_PROVIDER_SRCS) -o $@

check-security-key-policy: $(KEY_POLICY_TEST)
	$(KEY_POLICY_TEST)
	$(PYTHON) tests/security/key_policy_manifest_tests.py \
		--composer tools/generate_plugin_registry.py \
		--manifest-tool tools/make_ribos_qemu_manifest.py \
		--host-manifest $(HOST_MANIFEST)

check-security-key-policy-sanitizer: $(KEY_POLICY_SANITIZER_TEST)
	$(KEY_POLICY_SANITIZER_TEST)

$(PROTECTED_STATE_TEST): tests/security/protected_state_tests.c \
		$(SECURITY_PROTECTED_STATE_SRCS) \
		src/security/sha256.h include/Ribon/security/protected_state.h $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) \
		tests/security/protected_state_tests.c \
		$(SECURITY_PROTECTED_STATE_SRCS) -o $@

$(PROTECTED_STATE_SANITIZER_TEST): tests/security/protected_state_tests.c \
		$(SECURITY_PROTECTED_STATE_SRCS) \
		src/security/sha256.h include/Ribon/security/protected_state.h $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g $(WARNFLAGS) \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		tests/security/protected_state_tests.c \
		$(SECURITY_PROTECTED_STATE_SRCS) -o $@

check-security-protected-state: $(PROTECTED_STATE_TEST)
	$(PROTECTED_STATE_TEST)

check-security-protected-state-sanitizer: $(PROTECTED_STATE_SANITIZER_TEST)
	$(PROTECTED_STATE_SANITIZER_TEST)

$(UPDATE_MANIFEST_TEST): tests/update/manifest_tests.c \
		$(UPDATE_MANIFEST_SRCS) $(SECURITY_KEY_POLICY_SRCS) \
		$(SECURITY_PROVIDER_SRCS) include/Ribon/update/manifest.h $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(SECURITY_INCLUDE_FLAGS) \
		tests/update/manifest_tests.c $(UPDATE_MANIFEST_SRCS) \
		$(SECURITY_KEY_POLICY_SRCS) $(SECURITY_PROVIDER_SRCS) -o $@

$(UPDATE_MANIFEST_SANITIZER_TEST): tests/update/manifest_tests.c \
		$(UPDATE_MANIFEST_SRCS) $(SECURITY_KEY_POLICY_SRCS) \
		$(SECURITY_PROVIDER_SRCS) include/Ribon/update/manifest.h $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g $(WARNFLAGS) \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$(SECURITY_INCLUDE_FLAGS) tests/update/manifest_tests.c \
		$(UPDATE_MANIFEST_SRCS) $(SECURITY_KEY_POLICY_SRCS) \
		$(SECURITY_PROVIDER_SRCS) -o $@

check-update-manifest: $(UPDATE_MANIFEST_TEST)
	$(UPDATE_MANIFEST_TEST)
	$(PYTHON) tests/update/manifest_tool_tests.py \
		--c-codec $(UPDATE_MANIFEST_TEST) \
		--tool tools/update_manifest.py \
		--source tests/fixtures/update/update-manifest-source-v1.json \
		--vector tests/fixtures/update/update-manifest-vector-v1.json \
		--seed tests/fixtures/security/rfc8032-test1-seed.hex \
		--openssl $(OPENSSL)

check-update-manifest-sanitizer: $(UPDATE_MANIFEST_SANITIZER_TEST)
	$(UPDATE_MANIFEST_SANITIZER_TEST)

check-update-manifest-cross-compile:
	@set -e; \
	for target in x86_64 aarch64 riscv64; do \
		case "$$target" in \
			x86_64) compiler=$(X86_64_CC); triple=x86_64-none-elf;; \
			aarch64) compiler=$(AARCH64_CC); triple=aarch64-none-elf;; \
			riscv64) compiler=$(RISCV64_CC); triple=riscv64-none-elf;; \
		esac; \
		for source in $(UPDATE_MANIFEST_SRCS); do \
			object=$(UPDATE_MANIFEST_BUILD_DIR)/$$target/$${source%.c}.o; \
			mkdir -p "$$(dirname "$$object")"; \
			"$$compiler" --target="$$triple" $(FREESTANDING_FLAGS) \
				-c "$$source" -o "$$object"; \
		done; \
	done
	@echo "RIBON-UPDATE-MANIFEST-CROSS-COMPILE-OK targets=x86_64,aarch64,riscv64"

$(UPDATE_STORAGE_TEST): tests/update/storage_tests.c \
		tests/update/reference_storage.c tests/update/reference_storage.h \
		$(UPDATE_STORAGE_SRCS) $(SECURITY_KEY_POLICY_SRCS) \
		$(SECURITY_PROVIDER_SRCS) include/Ribon/update/storage.h $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(SECURITY_INCLUDE_FLAGS) \
		tests/update/storage_tests.c tests/update/reference_storage.c \
		$(UPDATE_STORAGE_SRCS) \
		$(SECURITY_KEY_POLICY_SRCS) $(SECURITY_PROVIDER_SRCS) -o $@

$(UPDATE_STORAGE_SANITIZER_TEST): tests/update/storage_tests.c \
		tests/update/reference_storage.c tests/update/reference_storage.h \
		$(UPDATE_STORAGE_SRCS) $(SECURITY_KEY_POLICY_SRCS) \
		$(SECURITY_PROVIDER_SRCS) include/Ribon/update/storage.h $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g $(WARNFLAGS) \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$(SECURITY_INCLUDE_FLAGS) tests/update/storage_tests.c \
		tests/update/reference_storage.c $(UPDATE_STORAGE_SRCS) \
		$(SECURITY_KEY_POLICY_SRCS) \
		$(SECURITY_PROVIDER_SRCS) -o $@

check-update-storage: $(UPDATE_STORAGE_TEST)
	$(UPDATE_STORAGE_TEST)
	$(PYTHON) tests/update/storage_tool_tests.py \
		--c-codec $(UPDATE_STORAGE_TEST) \
		--layout-tool tools/update_layout.py \
		--manifest-tool tools/update_manifest.py \
		--layout-source tests/fixtures/update/update-layout-source-v1.json \
		--manifest-source tests/fixtures/update/update-manifest-source-v1.json

check-update-storage-sanitizer: $(UPDATE_STORAGE_SANITIZER_TEST)
	$(UPDATE_STORAGE_SANITIZER_TEST)

check-update-storage-cross-compile:
	@set -e; \
	for target in x86_64 aarch64 riscv64; do \
		case "$$target" in \
			x86_64) compiler=$(X86_64_CC); triple=x86_64-none-elf;; \
			aarch64) compiler=$(AARCH64_CC); triple=aarch64-none-elf;; \
			riscv64) compiler=$(RISCV64_CC); triple=riscv64-none-elf;; \
		esac; \
		object=$(UPDATE_STORAGE_BUILD_DIR)/$$target/storage.o; \
		mkdir -p "$$(dirname "$$object")"; \
		"$$compiler" --target="$$triple" $(FREESTANDING_FLAGS) \
			-c src/update/storage.c -o "$$object"; \
	done
	@echo "RIBON-UPDATE-STORAGE-CROSS-COMPILE-OK targets=x86_64,aarch64,riscv64"

check-update-storage-graphs:
	@mkdir -p $(UPDATE_STORAGE_BUILD_DIR)
	$(PYTHON) tools/update_layout.py compose \
		--source tests/fixtures/update/update-layout-source-v1.json \
		--output $(UPDATE_STORAGE_BUILD_DIR)/layout-provenance.json
	$(PYTHON) tools/generate_plugin_registry.py \
		--manifest products/validation/manifests/update-storage-reference.json \
		--architecture x86_64 \
		--output $(UPDATE_STORAGE_BUILD_DIR)/plugin_registry.c \
		--report $(UPDATE_STORAGE_BUILD_DIR)/object-graph.json
	$(PYTHON) tools/lint/update_storage_graph_lint.py \
		--report $(UPDATE_STORAGE_BUILD_DIR)/object-graph.json \
		--layout $(UPDATE_STORAGE_BUILD_DIR)/layout-provenance.json
	$(PYTHON) tests/update/storage_graph_tests.py \
		--composer tools/generate_plugin_registry.py \
		--manifest products/validation/manifests/update-storage-reference.json

$(UPDATE_INSTALLER_TEST): tests/update/installer_tests.c \
		tests/update/reference_storage.c tests/update/reference_storage.h \
		src/update/installer.c $(UPDATE_STORAGE_SRCS) \
		$(SECURITY_KEY_POLICY_SRCS) $(SECURITY_PROVIDER_SRCS) \
		include/Ribon/update/installer.h $(UEFI_UPDATE_FIXTURE_STAMP) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(SECURITY_INCLUDE_FLAGS) \
		tests/update/installer_tests.c tests/update/reference_storage.c \
		src/update/installer.c $(UPDATE_STORAGE_SRCS) \
		$(SECURITY_KEY_POLICY_SRCS) $(SECURITY_PROVIDER_SRCS) -o $@

check-update-installer: $(UPDATE_INSTALLER_TEST)
	$(UPDATE_INSTALLER_TEST) \
		$(UEFI_UPDATE_FIXTURE_DIR)/update.man \
		$(UEFI_UPDATE_FIXTURE_DIR)/update.sig \
		$(UEFI_UPDATE_FIXTURE_DIR)/update.bin \
		$(UEFI_UPDATE_FIXTURE_DIR)/layout.bin

$(UPDATE_POWER_CUT_TEST): tests/update/power_cut_tests.c \
		src/update/transaction.c src/update/installer.c $(UPDATE_STORAGE_SRCS) \
		$(SECURITY_KEY_POLICY_SRCS) $(SECURITY_PROVIDER_SRCS) \
		include/Ribon/update/transaction.h include/Ribon/update/installer.h \
		$(UEFI_UPDATE_FIXTURE_STAMP) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(SECURITY_INCLUDE_FLAGS) \
		tests/update/power_cut_tests.c src/update/transaction.c \
		src/update/installer.c $(UPDATE_STORAGE_SRCS) \
		$(SECURITY_KEY_POLICY_SRCS) $(SECURITY_PROVIDER_SRCS) -o $@

$(UPDATE_POWER_CUT_SANITIZER_TEST): tests/update/power_cut_tests.c \
		src/update/transaction.c src/update/installer.c $(UPDATE_STORAGE_SRCS) \
		$(SECURITY_KEY_POLICY_SRCS) $(SECURITY_PROVIDER_SRCS) \
		include/Ribon/update/transaction.h include/Ribon/update/installer.h \
		$(UEFI_UPDATE_FIXTURE_STAMP) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g $(WARNFLAGS) \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$(SECURITY_INCLUDE_FLAGS) tests/update/power_cut_tests.c \
		src/update/transaction.c src/update/installer.c $(UPDATE_STORAGE_SRCS) \
		$(SECURITY_KEY_POLICY_SRCS) $(SECURITY_PROVIDER_SRCS) -o $@

check-update-power-cut-host: $(UPDATE_POWER_CUT_TEST)
	@mkdir -p $(UPDATE_POWER_CUT_CASES)
	$(UPDATE_POWER_CUT_TEST) \
		$(UEFI_UPDATE_FIXTURE_DIR)/update.man \
		$(UEFI_UPDATE_FIXTURE_DIR)/update.sig \
		$(UEFI_UPDATE_FIXTURE_DIR)/update.bin \
		$(UEFI_UPDATE_FIXTURE_DIR)/layout.bin \
		$(UEFI_UPDATE_DISK) $(UPDATE_POWER_CUT_CASES) \
		$(UPDATE_POWER_CUT_COVERAGE)

check-update-power-cut-sanitizer: $(UPDATE_POWER_CUT_SANITIZER_TEST)
	@mkdir -p $(UPDATE_POWER_CUT_DIR)/sanitizer-cases
	$(UPDATE_POWER_CUT_SANITIZER_TEST) \
		$(UEFI_UPDATE_FIXTURE_DIR)/update.man \
		$(UEFI_UPDATE_FIXTURE_DIR)/update.sig \
		$(UEFI_UPDATE_FIXTURE_DIR)/update.bin \
		$(UEFI_UPDATE_FIXTURE_DIR)/layout.bin \
		$(UEFI_UPDATE_DISK) $(UPDATE_POWER_CUT_DIR)/sanitizer-cases \
		$(UPDATE_POWER_CUT_DIR)/sanitizer-coverage.json

check-update-transaction-cross-compile:
	@set -e; \
	for target in x86_64 aarch64 riscv64; do \
		case "$$target" in \
			x86_64) compiler=$(X86_64_CC); triple=x86_64-none-elf;; \
			aarch64) compiler=$(AARCH64_CC); triple=aarch64-none-elf;; \
			riscv64) compiler=$(RISCV64_CC); triple=riscv64-none-elf;; \
		esac; \
		for source in src/update/installer.c src/update/transaction.c; do \
			object=$(UPDATE_POWER_CUT_DIR)/$$target/$${source%.c}.o; \
			mkdir -p "$$(dirname "$$object")"; \
			"$$compiler" --target="$$triple" $(FREESTANDING_FLAGS) \
				$(SECURITY_INCLUDE_FLAGS) -c "$$source" -o "$$object"; \
		done; \
	done
	@echo "RIBON-UPDATE-TRANSACTION-CROSS-COMPILE-OK targets=x86_64,aarch64,riscv64"

$(UEFI_UPDATE_STORAGE_TEST): tests/update/uefi_storage_tests.c \
		src/environments/uefi-app/update_storage.c $(UPDATE_STORAGE_SRCS) \
		$(SECURITY_KEY_POLICY_SRCS) $(SECURITY_PROVIDER_SRCS) \
		src/environments/uefi-app/update_storage.h $(UEFI_UPDATE_FIXTURE_STAMP) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(SECURITY_INCLUDE_FLAGS) \
		-Iinclude/uefi -Iinclude/uefi/X64 -fshort-wchar \
		tests/update/uefi_storage_tests.c \
		src/environments/uefi-app/update_storage.c $(UPDATE_STORAGE_SRCS) \
		$(SECURITY_KEY_POLICY_SRCS) $(SECURITY_PROVIDER_SRCS) -o $@

check-uefi-update-storage: $(UEFI_UPDATE_STORAGE_TEST)
	$(UEFI_UPDATE_STORAGE_TEST) $(UEFI_UPDATE_DISK)

$(RECOVERY_NETWORK_TEST): tests/network/recovery_network_tests.c \
		src/common/net/recovery.c src/common/net/tftp.c \
		src/core/service_directory.c include/Ribon/network/recovery.h \
		include/Ribon/network/tftp.h include/Ribon/service/directory.h $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) \
		tests/network/recovery_network_tests.c \
		src/common/net/recovery.c src/common/net/tftp.c \
		src/core/service_directory.c -o $@

$(RECOVERY_NETWORK_SANITIZER_TEST): tests/network/recovery_network_tests.c \
		src/common/net/recovery.c src/common/net/tftp.c \
		src/core/service_directory.c include/Ribon/network/recovery.h \
		include/Ribon/network/tftp.h include/Ribon/service/directory.h $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g $(WARNFLAGS) \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		tests/network/recovery_network_tests.c \
		src/common/net/recovery.c src/common/net/tftp.c \
		src/core/service_directory.c -o $@

check-recovery-network-host: $(RECOVERY_NETWORK_TEST)
	$(RECOVERY_NETWORK_TEST)

check-recovery-network-sanitizer: $(RECOVERY_NETWORK_SANITIZER_TEST)
	$(RECOVERY_NETWORK_SANITIZER_TEST)

check-recovery-network-cross-compile:
	@set -e; \
	for target in x86_64 aarch64 riscv64; do \
		case "$$target" in \
		x86_64) compiler=$(X86_64_CC); triple=x86_64-none-elf;; \
		aarch64) compiler=$(AARCH64_CC); triple=aarch64-none-elf;; \
		riscv64) compiler=$(RISCV64_CC); triple=riscv64-none-elf;; \
		esac; \
		for source in src/common/net/recovery.c src/common/net/tftp.c; do \
			object=$(BUILD_ROOT)/recovery-network/$$target/$${source%.c}.o; \
			mkdir -p "$$(dirname "$$object")"; \
			"$$compiler" --target="$$triple" $(FREESTANDING_FLAGS) \
				-c "$$source" -o "$$object"; \
		done; \
	done
	@echo "RIBON-D05-RECOVERY-NETWORK-CROSS-COMPILE-OK targets=x86_64,aarch64,riscv64"

check-recovery-network-graphs: $(UEFI_NETWORK_UPDATE_GRAPH) \
		check-normal-media-surface
	$(PYTHON) tests/network/recovery_network_graph_tests.py \
		--composer tools/generate_plugin_registry.py \
		--manifest $(UEFI_NETWORK_UPDATE_MANIFEST)

check-security-ed25519-cross-compile:
	@set -e; \
	for target in x86_64 aarch64 riscv64; do \
		case "$$target" in \
			x86_64) compiler=$(X86_64_CC); triple=x86_64-none-elf;; \
			aarch64) compiler=$(AARCH64_CC); triple=aarch64-none-elf;; \
			riscv64) compiler=$(RISCV64_CC); triple=riscv64-none-elf;; \
		esac; \
		for source in $(SECURITY_PROVIDER_SRCS); do \
			object=$(SECURITY_BUILD_DIR)/$$target/$${source%.c}.o; \
			mkdir -p "$$(dirname "$$object")"; \
			"$$compiler" --target="$$triple" $(FREESTANDING_FLAGS) \
				$(SECURITY_INCLUDE_FLAGS) -c "$$source" -o "$$object"; \
		done; \
	done
	@echo "RIBON-ED25519-CROSS-COMPILE-OK targets=x86_64,aarch64,riscv64"

check-security-provider-graphs: check-ribos-cross-arch-objects
	$(PYTHON) tools/lint/security_provider_graph_lint.py \
		--manifest $(RIBOS_R18_MANIFEST) \
		--graph $(RIBOS_R18_AMD64_GRAPH) \
		--graph $(RIBOS_R18_AARCH64_GRAPH) \
		--graph $(RIBOS_R18_RISCV64_GRAPH) \
		--map $(RIBOS_R18_AMD64_DIR)/ribon-ribos.map \
		--map $(RIBOS_R18_AARCH64_DIR)/ribon-ribos.map \
		--map $(RIBOS_R18_RISCV64_DIR)/ribon-ribos.map \
		--image $(RIBOS_R18_AMD64_APP) \
		--image $(RIBOS_R18_AARCH64_ELF) \
		--image $(RIBOS_R18_RISCV64_ELF) \
		--private-seed tests/fixtures/security/rfc8032-test1-seed.hex

check-security-key-policy-graphs: check-ribos-cross-arch-objects
	$(PYTHON) tools/lint/key_policy_graph_lint.py \
		--manifest $(RIBOS_R18_MANIFEST) \
		--graph $(RIBOS_R18_AMD64_GRAPH) \
		--graph $(RIBOS_R18_AARCH64_GRAPH) \
		--graph $(RIBOS_R18_RISCV64_GRAPH) \
		--map $(RIBOS_R18_AMD64_DIR)/ribon-ribos.map \
		--map $(RIBOS_R18_AARCH64_DIR)/ribon-ribos.map \
		--map $(RIBOS_R18_RISCV64_DIR)/ribon-ribos.map \
		--image $(RIBOS_R18_AMD64_APP) \
		--image $(RIBOS_R18_AARCH64_ELF) \
		--image $(RIBOS_R18_RISCV64_ELF)

check-security-protected-state-graphs: check-ribos-cross-arch-objects
	$(PYTHON) tools/lint/protected_state_graph_lint.py \
		--manifest $(RIBOS_R18_MANIFEST) \
		--graph $(RIBOS_R18_AMD64_GRAPH) \
		--graph $(RIBOS_R18_AARCH64_GRAPH) \
		--graph $(RIBOS_R18_RISCV64_GRAPH) \
		--map $(RIBOS_R18_AMD64_DIR)/ribon-ribos.map \
		--map $(RIBOS_R18_AARCH64_DIR)/ribon-ribos.map \
		--map $(RIBOS_R18_RISCV64_DIR)/ribon-ribos.map

check-ribos-cross-arch-qemu: \
		$(RIBOS_R18_AMD64_ESP)/EFI/BOOT/BOOTX64.EFI \
		$(RIBOS_R18_AARCH64_IMAGE) \
		$(RIBOS_R18_RISCV64_IMAGE) \
		$(RIBOS_R18_ARTIFACT) $(RIBOS_R18_TRIAL_ARTIFACT) \
		$(RIBOS_R18_MANIFEST) $(RIBOS_R18_AMD64_GRAPH) \
		$(RIBOS_R18_AARCH64_GRAPH) $(RIBOS_R18_RISCV64_GRAPH)
	$(PYTHON) tools/ribos_cross_arch_qemu.py \
		--qemu-x86-64 $(QEMU_X86_64) \
		--qemu-aarch64 $(QEMU_AARCH64) \
		--qemu-riscv64 $(QEMU_RISCV64) \
		--x86-64-esp $(RIBOS_R18_AMD64_ESP) \
		--x86-64-firmware $(X86_64_UEFI_FIRMWARE) \
		--aarch64-image $(RIBOS_R18_AARCH64_IMAGE) \
		--riscv64-image $(RIBOS_R18_RISCV64_IMAGE) \
		--riscv64-firmware $(RISCV64_OPENSBI_FIRMWARE) \
		--artifact $(RIBOS_R18_ARTIFACT) \
		--trial-artifact $(RIBOS_R18_TRIAL_ARTIFACT) \
		--product-manifest $(RIBOS_R18_MANIFEST) \
		--graph $(RIBOS_R18_AMD64_GRAPH) \
		--graph $(RIBOS_R18_AARCH64_GRAPH) \
		--graph $(RIBOS_R18_RISCV64_GRAPH) \
		--source-revision $(shell git rev-parse HEAD) \
		--output-dir $(RESULTS_DIR)/ribos-r18

check-ribos-r18: check-ribos-golden-artifact \
		check-ribos-cross-arch-objects check-security-provider-graphs \
		check-security-key-policy-graphs \
		check-security-protected-state-graphs \
		check-ribos-cross-arch-qemu
	@echo "RIBOS-R18-AGGREGATE-OK artifacts=confirmed,trial targets=3 \
qemu=guest-executed negative=target hostile=bounded hardware=not-run"

ribos-r18-release-artifacts: check-ribos-golden-artifact \
		$(RIBOS_R18_AMD64_APP) $(RIBOS_R18_AARCH64_IMAGE) \
		$(RIBOS_R18_RISCV64_IMAGE)

check-ribos-release-reproducibility:
	$(PYTHON) tools/check_ribos_release_reproducibility.py \
		--make $(MAKE) --root $(ROOT) \
		--work-root $(BUILD_ROOT)/tests/ribos-release-reproducibility

check-ribos-production-policy: check-uefi-product-hermeticity \
		check-ribos-executable-corpus check-ribos-hostile \
		check-security-ed25519-provider check-security-ed25519-sanitizer \
		check-security-ed25519-cross-compile check-security-provider-graphs \
		check-security-key-policy check-security-key-policy-sanitizer \
		check-security-key-policy-graphs check-security-protected-state \
		check-security-protected-state-sanitizer \
		check-security-protected-state-graphs \
		check-ribos-ribon-integration check-ribos-product-graphs \
		check-ribos-normal-no-network check-ribos-factory-recovery \
		check-ribos-r18 check-ribos-release-reproducibility
	@echo "RIBOS-PRODUCTION-POLICY-OK authorization=ed25519-key-policy-rollback \
targets=amd64,aarch64,riscv64 qemu=runtime reproducible=yes hardware=not-run"

$(KERNEL_FIXTURE): tools/make_elf64_fixture.py
	@mkdir -p $(@D)
	$(PYTHON) $< --arch $(RIBON_ARCH) --output $@

$(LOADER_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/loader/elf64_loader_tests.o \
	$(TEST_BUILD_DIR)/obj/src/image-formats/elf64.o \
	$(TEST_BUILD_DIR)/obj/src/arch/common.o \
	$(TEST_BUILD_DIR)/obj/src/common/image.o
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(PE_COFF_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/loader/pe_coff_loader_tests.o \
	$(TEST_BUILD_DIR)/obj/src/image-formats/pe_coff.o \
	$(TEST_BUILD_DIR)/obj/src/arch/common.o \
	$(TEST_BUILD_DIR)/obj/src/common/image.o
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(FDT_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/fdt/fdt_parser_tests.o \
	$(TEST_BUILD_DIR)/obj/src/common/sys/fdt/fdt.o
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(RPH1_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/rph1/rph1_builder_tests.o \
	$(TEST_BUILD_DIR)/obj/src/protocols/os/parus/rph1_builder.o \
	$(TEST_BUILD_DIR)/obj/src/protocols/os/parus/rph1_parser.o \
	$(TEST_BUILD_DIR)/obj/src/core/memory.o
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(ARCH_X86_64_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/arch/x86_64_direct_high_tests.o \
	$(TEST_BUILD_DIR)/obj/src/arch/common.o \
	$(TEST_BUILD_DIR)/obj/src/arch/x86_64/arch.o
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(ARCH_AARCH64_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/arch/aarch64_direct_high_tests.o \
	$(TEST_BUILD_DIR)/obj/src/arch/common.o \
	$(TEST_BUILD_DIR)/obj/src/arch/aarch64/arch.o
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(TEST_BUILD_DIR)/arch_ops_%_tests: \
	$(TEST_BUILD_DIR)/obj/tests/arch/arch_ops_tests.o \
	$(TEST_BUILD_DIR)/obj/src/arch/common.o \
	$(TEST_BUILD_DIR)/obj/src/arch/%/arch.o
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(CORE_SERVICE_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/core/service_boundary_tests.o \
	$(ARCH_OBJS) $(HOST_PRODUCT_OBJS) $(GENERATED_REGISTRY_O) \
	$(RIBOS_POLICY_LIB) $(BOOT_LIB) $(CORE_LIB) $(RIBOS_TARGET_CORE_LIB) \
	$(HOST_SECURITY_OBJS)
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(PORT_SERVICE_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/core/port_service_tests.o \
	$(TEST_BUILD_DIR)/obj/src/common/drivers/serial/pl011.o \
	$(TEST_BUILD_DIR)/obj/ports/qemu/virt-aarch64/port.o \
	$(BOOT_LIB) $(CORE_LIB)
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(BOOT_LIFECYCLE_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/boot/lifecycle_tests.o \
	$(ARCH_OBJS) $(HOST_PRODUCT_OBJS) $(GENERATED_REGISTRY_O) \
	$(RIBOS_POLICY_LIB) $(BOOT_LIB) $(CORE_LIB) $(RIBOS_TARGET_CORE_LIB) \
	$(HOST_SECURITY_OBJS)
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(ENVIRONMENT_PERSISTENT_INPUTS_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/boot/environment_persistent_inputs_tests.o \
	$(TEST_BUILD_DIR)/obj/src/common/environment.o
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(BOOT_MODULE_BUNDLE_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/boot/module_bundle_tests.o \
	$(TEST_BUILD_DIR)/obj/src/common/module_bundle.o
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(RAW_FDT_CAPACITY_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/fdt/raw_fdt_capacity_tests.o \
	$(TEST_BUILD_DIR)/obj/src/environments/raw-fdt/raw_fdt.o \
	$(TEST_BUILD_DIR)/obj/src/common/sys/fdt/fdt.o \
	$(TEST_BUILD_DIR)/obj/src/common/environment.o
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(MEDIA_PIPELINE_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/media/media_pipeline_tests.o \
	$(TEST_BUILD_DIR)/obj/src/config/boot_config.o \
	$(TEST_BUILD_DIR)/obj/src/filesystems/fat32.o \
	$(TEST_BUILD_DIR)/obj/src/storage/block.o \
	$(TEST_BUILD_DIR)/obj/src/storage/gpt.o
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(PLUGIN_DESCRIPTOR_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/plugin/descriptor_tests.o $(CORE_LIB)
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(PROTOCOL_CONTRACT_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/protocol/contract_tests.o \
	$(ARCH_OBJS) $(BUILD_DIR)/obj/src/protocols/synthetic/protocol.o \
	$(BOOT_LIB) $(CORE_LIB)
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(PARUS_ENTRY_CONTRACT_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/protocol/parus_entry_contract_tests.o \
	$(TEST_BUILD_DIR)/obj/src/protocols/os/parus/protocol.o \
	$(TEST_BUILD_DIR)/obj/src/protocols/os/parus/rph1_builder.o \
	$(TEST_BUILD_DIR)/obj/src/protocols/os/parus/rph1_parser.o \
	$(TEST_BUILD_DIR)/obj/src/common/protocol.o \
	$(TEST_BUILD_DIR)/obj/src/core/memory.o
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(OS_PACKAGE_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/protocol/os_package_tests.o \
	$(TEST_BUILD_DIR)/obj/src/protocols/os/linux/fdt.o \
	$(TEST_BUILD_DIR)/obj/src/protocols/os/linux/protocol.o \
	$(TEST_BUILD_DIR)/obj/src/protocols/os/linux/efi.o \
	$(TEST_BUILD_DIR)/obj/src/protocols/os/freebsd/protocol.o \
	$(TEST_BUILD_DIR)/obj/src/protocols/os/zircon/protocol.o \
	$(TEST_BUILD_DIR)/obj/src/common/protocol.o \
	$(TEST_BUILD_DIR)/obj/src/core/memory.o
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(LINUX_BOOT_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/protocol/linux_boot_tests.o \
	$(TEST_BUILD_DIR)/obj/src/protocols/os/linux/fdt.o \
	$(TEST_BUILD_DIR)/obj/src/protocols/os/linux/protocol.o \
	$(TEST_BUILD_DIR)/obj/src/image-formats/linux_aarch64.o \
	$(TEST_BUILD_DIR)/obj/src/image-formats/linux_riscv64.o \
	$(TEST_BUILD_DIR)/obj/src/common/image.o \
	$(TEST_BUILD_DIR)/obj/src/common/protocol.o \
	$(TEST_BUILD_DIR)/obj/src/core/memory.o
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(PROTOCOL_FREE_EMBED_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/library/protocol_free_embed_tests.o $(CORE_LIB)
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(TEST_BUILD_DIR)/mode_descriptor_%_tests: \
	$(TEST_BUILD_DIR)/obj/tests/core/mode_descriptor_tests.o \
	$(TEST_BUILD_DIR)/obj/src/modes/%.o $(CORE_LIB)
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(QEMU_RAW_REGISTRY_C): $(QEMU_RAW_MANIFEST) tools/generate_plugin_registry.py
	$(PYTHON) tools/generate_plugin_registry.py --manifest $< \
		--output $@ --report $(QEMU_RAW_GRAPH)

$(QEMU_RAW_FIXTURE): tools/make_elf64_fixture.py
	@mkdir -p $(@D)
	$(PYTHON) $< --arch aarch64 --base 0x41000000 --entry-at-base --output $@

$(QEMU_RAW_EMBED_C): $(QEMU_RAW_PAYLOAD) tools/embed_binary.py
	$(PYTHON) tools/embed_binary.py --input $< --output $@
