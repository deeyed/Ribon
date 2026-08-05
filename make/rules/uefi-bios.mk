uefi-external-input-force:

$(UEFI_FIXTURE_REGISTRY_C): \
	$(UEFI_FIXTURE_MANIFEST) tools/generate_plugin_registry.py
	$(PYTHON) tools/generate_plugin_registry.py --manifest $< \
		--output $@ --report $(UEFI_FIXTURE_GRAPH)

$(UEFI_EXTERNAL_REGISTRY_C): \
	$(UEFI_EXTERNAL_MANIFEST) tools/generate_plugin_registry.py
	$(PYTHON) tools/generate_plugin_registry.py --manifest $< \
		--output $@ --report $(UEFI_EXTERNAL_GRAPH)

$(UEFI_LINUX_REGISTRY_C): $(UEFI_LINUX_MANIFEST) tools/generate_plugin_registry.py
	$(PYTHON) tools/generate_plugin_registry.py --manifest $< \
		--output $@ --report $(UEFI_LINUX_GRAPH)

$(UEFI_FREEBSD_REGISTRY_C): $(UEFI_FREEBSD_MANIFEST) tools/generate_plugin_registry.py
	$(PYTHON) tools/generate_plugin_registry.py --manifest $< \
		--output $@ --report $(UEFI_FREEBSD_GRAPH)

$(UEFI_LINUX_INPUT_MANIFEST): $(UEFI_LINUX_MANIFEST)
	@mkdir -p $(@D)
	cp $< $@

$(UEFI_FREEBSD_INPUT_MANIFEST): $(UEFI_FREEBSD_MANIFEST)
	@mkdir -p $(@D)
	cp $< $@

$(UEFI_LINUX_EXTERNAL_STAMP): $(UEFI_LINUX_INPUT_DESCRIPTOR) \
		tools/prepare_external_linux_efi.py
	$(PYTHON) tools/prepare_external_linux_efi.py \
		--descriptor $(UEFI_LINUX_INPUT_DESCRIPTOR) --cache $(UEFI_LINUX_CACHE) \
		--result $(UEFI_LINUX_EXTERNAL_VALIDATION) --allow-download
	@touch $@

$(UEFI_LINUX_EXTERNAL_VALIDATION): $(UEFI_LINUX_EXTERNAL_STAMP)
	@test -f $@

$(UEFI_FREEBSD_EXTERNAL_STAMP): $(UEFI_FREEBSD_INPUT_DESCRIPTOR) \
		tools/prepare_external_freebsd.py
	$(PYTHON) tools/prepare_external_freebsd.py \
		--descriptor $(UEFI_FREEBSD_INPUT_DESCRIPTOR) \
		--compressed-cache $(UEFI_FREEBSD_COMPRESSED_CACHE) \
		--raw-cache $(UEFI_FREEBSD_RAW_CACHE) \
		--result $(UEFI_FREEBSD_EXTERNAL_VALIDATION) --allow-download
	@touch $@

$(UEFI_FREEBSD_EXTERNAL_VALIDATION): $(UEFI_FREEBSD_EXTERNAL_STAMP)
	@test -f $@

$(UEFI_FIXTURE_PAYLOAD_SOURCE): tools/make_elf64_fixture.py
	@mkdir -p $(@D)
	$(PYTHON) $< --arch x86_64 --base 0x200000 --entry-at-base --output $@

$(UEFI_FIXTURE_DIR)/obj/%.o: %.c $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(X86_64_CC) $(UEFI_FLAGS) $(DEPFLAGS) -c $< -o $@

$(UEFI_EXTERNAL_DIR)/obj/%.o: %.c $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(X86_64_CC) $(UEFI_FLAGS) $(DEPFLAGS) -c $< -o $@

$(UEFI_LINUX_DIR)/obj/%.o: %.c $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(X86_64_CC) $(UEFI_FLAGS) $(DEPFLAGS) -c $< -o $@

$(UEFI_FREEBSD_DIR)/obj/%.o: %.c $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(X86_64_CC) $(UEFI_FLAGS) $(DEPFLAGS) -c $< -o $@

$(UEFI_FIXTURE_DIR)/obj/generated/plugin_registry.o: \
	$(UEFI_FIXTURE_REGISTRY_C)
	@mkdir -p $(@D)
	$(X86_64_CC) $(UEFI_FLAGS) $(DEPFLAGS) -c $< -o $@

$(UEFI_EXTERNAL_DIR)/obj/generated/plugin_registry.o: \
	$(UEFI_EXTERNAL_REGISTRY_C)
	@mkdir -p $(@D)
	$(X86_64_CC) $(UEFI_FLAGS) $(DEPFLAGS) -c $< -o $@

$(UEFI_LINUX_DIR)/obj/generated/plugin_registry.o: $(UEFI_LINUX_REGISTRY_C)
	@mkdir -p $(@D)
	$(X86_64_CC) $(UEFI_FLAGS) $(DEPFLAGS) -c $< -o $@

$(UEFI_FREEBSD_DIR)/obj/generated/plugin_registry.o: $(UEFI_FREEBSD_REGISTRY_C)
	@mkdir -p $(@D)
	$(X86_64_CC) $(UEFI_FLAGS) $(DEPFLAGS) -c $< -o $@

$(UEFI_FIXTURE_APP): $(UEFI_FIXTURE_OBJS)
	$(LLD_LINK) /Brepro /subsystem:efi_application /entry:efi_main /nodefaultlib \
		/machine:x64 /map:$(UEFI_FIXTURE_DIR)/ribon.map /out:$@ \
		$(UEFI_FIXTURE_OBJS)

$(UEFI_EXTERNAL_APP): $(UEFI_EXTERNAL_OBJS)
	$(LLD_LINK) /Brepro /subsystem:efi_application /entry:efi_main /nodefaultlib \
		/machine:x64 /map:$(UEFI_EXTERNAL_DIR)/ribon.map /out:$@ \
		$(UEFI_EXTERNAL_OBJS)

$(UEFI_LINUX_APP): $(UEFI_LINUX_OBJS)
	$(LLD_LINK) /Brepro /subsystem:efi_application /entry:efi_main /nodefaultlib \
		/machine:x64 /map:$(UEFI_LINUX_DIR)/ribon.map /out:$@ \
		$(UEFI_LINUX_OBJS)

$(UEFI_FREEBSD_APP): $(UEFI_FREEBSD_OBJS)
	$(LLD_LINK) /Brepro /subsystem:efi_application /entry:efi_main /nodefaultlib \
		/machine:x64 /map:$(UEFI_FREEBSD_DIR)/ribon.map /out:$@ \
		$(UEFI_FREEBSD_OBJS)

$(UEFI_FIXTURE_ESP)/EFI/BOOT/BOOTX64.EFI: $(UEFI_FIXTURE_APP)
	@mkdir -p $(@D)
	cp $< $@

$(UEFI_EXTERNAL_ESP)/EFI/BOOT/BOOTX64.EFI: $(UEFI_EXTERNAL_APP)
	@mkdir -p $(@D)
	cp $< $@

$(UEFI_LINUX_ESP)/EFI/BOOT/BOOTX64.EFI: $(UEFI_LINUX_APP)
	@mkdir -p $(@D)
	cp $< $@

$(UEFI_FIXTURE_CONFIG): tools/make_boot_config.py $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(PYTHON) $< --output $@ --entry primary --priority 100 \
		--protocol protocol.parus --image image.elf64 --kernel /RIBON/PAYLOAD.ELF \
		--init-image /RIBON/INIT.IMG

$(UEFI_EXTERNAL_CONFIG): tools/make_boot_config.py $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(PYTHON) $< --output $@ --entry primary --priority 100 \
		--protocol protocol.parus --image image.elf64 --kernel /RIBON/PAYLOAD.ELF \
		--init-image /RIBON/INIT.IMG

$(UEFI_LINUX_CONFIG): tools/make_boot_config.py $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(PYTHON) $< --output $@ --entry primary --priority 100 \
		--protocol protocol.linux-efi --image image.pe-coff \
		--kernel /RIBON/LINUX.EFI \
		--cmdline 'console=ttyS0 rdinit=/init panic=-1 initrd=\RIBON\INITRD.CPIO'

$(UEFI_FREEBSD_CONFIG): tools/make_boot_config.py $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(PYTHON) $< --output $@ --entry primary --priority 100 \
		--protocol protocol.freebsd --image image.pe-coff \
		--kernel /EFI/FREEBSD/LOADER.EFI --cmdline='-h -s'

$(UEFI_LINUX_PAYLOAD): $(UEFI_LINUX_EXTERNAL_STAMP)
	@mkdir -p $(@D)
	cp $(UEFI_LINUX_CACHE) $@

$(UEFI_LINUX_INIT_OBJ): tests/fixtures/linux/x86_64/init.S
	@mkdir -p $(@D)
	$(X86_64_CC) --target=x86_64-none-elf -c $< -o $@

$(UEFI_LINUX_INIT_ELF): $(UEFI_LINUX_INIT_OBJ) tests/fixtures/linux/x86_64/init.ld
	$(LD_LLD) -m elf_x86_64 -static -T tests/fixtures/linux/x86_64/init.ld \
		-o $@ $(UEFI_LINUX_INIT_OBJ)

$(UEFI_LINUX_INITRAMFS_STAMP): $(UEFI_LINUX_INIT_ELF) tools/build_linux_initramfs.py
	$(PYTHON) tools/build_linux_initramfs.py --architecture x86_64 \
		--init $(UEFI_LINUX_INIT_ELF) --output $(UEFI_LINUX_INITRAMFS) \
		--component-manifest $(UEFI_LINUX_INITRAMFS_MANIFEST)
	@touch $@

$(UEFI_LINUX_INITRAMFS) $(UEFI_LINUX_INITRAMFS_MANIFEST): \
		$(UEFI_LINUX_INITRAMFS_STAMP)
	@test -f $@

$(UEFI_FREEBSD_PACKAGE_STAMP): $(UEFI_FREEBSD_EXTERNAL_STAMP) \
		$(UEFI_FREEBSD_APP) $(UEFI_FREEBSD_CONFIG) \
		tools/compose_freebsd_uefi.py
	$(PYTHON) tools/compose_freebsd_uefi.py \
		--source $(UEFI_FREEBSD_RAW_CACHE) --ribon-app $(UEFI_FREEBSD_APP) \
		--config $(UEFI_FREEBSD_CONFIG) --output $(UEFI_FREEBSD_DISK) \
		--loader-output $(UEFI_FREEBSD_LOADER) \
		--result $(UEFI_FREEBSD_PACKAGE)
	@touch $@

$(UEFI_FREEBSD_DISK) $(UEFI_FREEBSD_LOADER) $(UEFI_FREEBSD_PACKAGE): \
		$(UEFI_FREEBSD_PACKAGE_STAMP)
	@test -f $@

$(UEFI_FIXTURE_PAYLOAD): $(UEFI_FIXTURE_PAYLOAD_SOURCE)
	@mkdir -p $(@D)
	cp $< $@

$(UEFI_EXTERNAL_PAYLOAD): \
	$(UEFI_EXTERNAL_SOURCE) $(UEFI_EXTERNAL_VALIDATION) \
	uefi-external-input-force
	@test -n "$(UEFI_EXTERNAL_SOURCE)" || \
		{ echo "UEFI_PARUS_PAYLOAD is required" >&2; exit 2; }
	@mkdir -p $(@D)
	cp $(UEFI_EXTERNAL_SOURCE) $@

$(UEFI_FIXTURE_INIT_SOURCE): tools/make_init_image_fixture.py
	@mkdir -p $(@D)
	$(PYTHON) $< --output $@

$(UEFI_EXTERNAL_INIT_SOURCE): tools/make_init_image_fixture.py
	@mkdir -p $(@D)
	$(PYTHON) $< --output $@

$(UEFI_FIXTURE_INIT_IMAGE): $(UEFI_FIXTURE_INIT_SOURCE)
	@mkdir -p $(@D)
	cp $< $@

$(UEFI_EXTERNAL_INIT_IMAGE): $(UEFI_EXTERNAL_INIT_SOURCE)
	@mkdir -p $(@D)
	cp $< $@

x86_64-uefi-parus-fixture: \
	$(UEFI_FIXTURE_ESP)/EFI/BOOT/BOOTX64.EFI \
	$(UEFI_FIXTURE_CONFIG) $(UEFI_FIXTURE_PAYLOAD) \
	$(UEFI_FIXTURE_INIT_IMAGE) $(UEFI_FIXTURE_INPUT_MANIFEST)

x86_64-uefi-parus-external-product: \
	$(UEFI_EXTERNAL_ESP)/EFI/BOOT/BOOTX64.EFI \
	$(UEFI_EXTERNAL_CONFIG) $(UEFI_EXTERNAL_PAYLOAD) \
	$(UEFI_EXTERNAL_INIT_IMAGE) $(UEFI_EXTERNAL_INPUT_MANIFEST)

x86_64-uefi-parus-external:
	@test -n "$(UEFI_PARUS_PAYLOAD)" || \
		{ echo "UEFI_PARUS_PAYLOAD is required" >&2; exit 2; }
	$(MAKE) --no-print-directory \
		UEFI_PARUS_PAYLOAD=$(abspath $(UEFI_PARUS_PAYLOAD)) \
		x86_64-uefi-parus-external-product

x86_64-uefi-linux-product: \
		$(UEFI_LINUX_ESP)/EFI/BOOT/BOOTX64.EFI \
		$(UEFI_LINUX_CONFIG) $(UEFI_LINUX_PAYLOAD) \
		$(UEFI_LINUX_INITRAMFS) $(UEFI_LINUX_INPUT_MANIFEST) \
		$(UEFI_LINUX_EXTERNAL_VALIDATION)

x86_64-uefi-freebsd-product: $(UEFI_FREEBSD_DISK) \
		$(UEFI_FREEBSD_LOADER) $(UEFI_FREEBSD_PACKAGE) \
		$(UEFI_FREEBSD_INPUT_MANIFEST) $(UEFI_FREEBSD_EXTERNAL_VALIDATION)

$(UEFI_UPDATE_REGISTRY_C): \
	$(UEFI_UPDATE_MANIFEST) tools/generate_plugin_registry.py
	$(PYTHON) tools/generate_plugin_registry.py --manifest $< \
		--output $@ --report $(UEFI_UPDATE_GRAPH)

$(UEFI_UPDATE_FIXTURE_STAMP): \
	$(UEFI_UPDATE_MANIFEST) tests/fixtures/update/update-layout-source-v1.json \
	tools/make_qemu_update_fixture.py tools/update_manifest.py tools/update_layout.py \
	tools/boot_confirmation.py
	$(PYTHON) tools/make_qemu_update_fixture.py \
		--product-manifest $(UEFI_UPDATE_MANIFEST) \
		--layout-source tests/fixtures/update/update-layout-source-v1.json \
		--output-root $(UEFI_UPDATE_FIXTURE_DIR)
	@touch $@

$(UEFI_UPDATE_DIR)/obj/%.o: %.c $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(X86_64_CC) $(UEFI_FLAGS) $(SECURITY_INCLUDE_FLAGS) $(DEPFLAGS) -c $< -o $@

$(UEFI_UPDATE_DIR)/obj/generated/plugin_registry.o: $(UEFI_UPDATE_REGISTRY_C)
	@mkdir -p $(@D)
	$(X86_64_CC) $(UEFI_FLAGS) $(SECURITY_INCLUDE_FLAGS) $(DEPFLAGS) -c $< -o $@

$(UEFI_UPDATE_APP): $(UEFI_UPDATE_OBJS)
	$(LLD_LINK) /Brepro /subsystem:efi_application /entry:efi_main /nodefaultlib \
		/machine:x64 /map:$(UEFI_UPDATE_DIR)/ribon.map /out:$@ \
		$(UEFI_UPDATE_OBJS)

$(UEFI_UPDATE_ESP)/EFI/BOOT/BOOTX64.EFI: $(UEFI_UPDATE_APP)
	@mkdir -p $(@D)
	cp $< $@

$(UEFI_UPDATE_ESP)/RIBON/UPDATE.MAN: $(UEFI_UPDATE_FIXTURE_STAMP)
	@mkdir -p $(@D)
	cp $(UEFI_UPDATE_FIXTURE_DIR)/update.man $@

$(UEFI_UPDATE_ESP)/RIBON/UPDATE.SIG: $(UEFI_UPDATE_FIXTURE_STAMP)
	@mkdir -p $(@D)
	cp $(UEFI_UPDATE_FIXTURE_DIR)/update.sig $@

$(UEFI_UPDATE_ESP)/RIBON/UPDATE.BIN: $(UEFI_UPDATE_FIXTURE_STAMP)
	@mkdir -p $(@D)
	cp $(UEFI_UPDATE_FIXTURE_DIR)/update.bin $@

$(UEFI_UPDATE_ESP)/RIBON/CONFIRM.BIN: $(UEFI_UPDATE_FIXTURE_STAMP)
	@mkdir -p $(@D)
	cp $(UEFI_UPDATE_FIXTURE_DIR)/confirmation.bin $@

x86_64-uefi-update-recovery: \
	$(UEFI_UPDATE_ESP)/EFI/BOOT/BOOTX64.EFI \
	$(UEFI_UPDATE_ESP)/RIBON/UPDATE.MAN \
	$(UEFI_UPDATE_ESP)/RIBON/UPDATE.SIG \
	$(UEFI_UPDATE_ESP)/RIBON/UPDATE.BIN \
	$(UEFI_UPDATE_ESP)/RIBON/CONFIRM.BIN \
	$(UEFI_UPDATE_FIXTURE_STAMP)

x86_64-uefi-update-recovery-smoke: x86_64-uefi-update-recovery
	$(PYTHON) tools/qemu_update_install.py \
		--qemu $(QEMU_X86_64) --firmware $(X86_64_UEFI_FIRMWARE) \
		--esp $(UEFI_UPDATE_ESP) --disk $(UEFI_UPDATE_DISK) \
		--boot-app $(UEFI_UPDATE_APP) \
		--manifest $(UEFI_UPDATE_FIXTURE_DIR)/update.man \
		--envelope $(UEFI_UPDATE_FIXTURE_DIR)/update.sig \
		--bundle $(UEFI_UPDATE_FIXTURE_DIR)/update.bin \
		--product-manifest $(UEFI_UPDATE_MANIFEST) \
		--fixture-provenance $(UEFI_UPDATE_FIXTURE_PROVENANCE) \
		--inspector tools/inspect_qemu_update_disk.py \
		--source-revision $(shell git rev-parse HEAD) \
		--results $(UEFI_UPDATE_RESULTS)

check-qemu-update-install: x86_64-uefi-update-recovery-smoke
	$(PYTHON) tests/update/qemu_update_fixture_tests.py \
		--generator tools/make_qemu_update_fixture.py \
		--product-manifest $(UEFI_UPDATE_MANIFEST) \
		--layout-source tests/fixtures/update/update-layout-source-v1.json \
		--inspector tools/inspect_qemu_update_disk.py \
		--installed-disk $(UEFI_UPDATE_RESULTS)/update-disk-runtime.raw \
		--manifest $(UEFI_UPDATE_FIXTURE_DIR)/update.man \
		--provenance $(UEFI_UPDATE_FIXTURE_PROVENANCE)

check-qemu-update-power-cut: check-update-power-cut-host \
		x86_64-uefi-update-recovery
	$(PYTHON) tools/qemu_update_power_cut.py \
		--qemu $(QEMU_X86_64) --firmware $(X86_64_UEFI_FIRMWARE) \
		--esp-template $(UEFI_UPDATE_ESP) --boot-app $(UEFI_UPDATE_APP) \
		--manifest $(UEFI_UPDATE_FIXTURE_DIR)/update.man \
		--bundle $(UEFI_UPDATE_FIXTURE_DIR)/update.bin \
		--product-manifest $(UEFI_UPDATE_MANIFEST) \
		--fixture-provenance $(UEFI_UPDATE_FIXTURE_PROVENANCE) \
		--coverage $(UPDATE_POWER_CUT_COVERAGE) \
		--case-root $(UPDATE_POWER_CUT_CASES) \
		--inspector tools/inspect_qemu_update_transaction.py \
		--source-revision $(shell git rev-parse HEAD) \
		--results $(UEFI_UPDATE_RESULTS)/power-cut

check-update-power-cut: check-update-power-cut-host \
		check-update-power-cut-sanitizer \
		check-update-transaction-cross-compile check-qemu-update-power-cut

$(BOOT_CONFIRMATION_TEST): tests/update/confirmation_tests.c \
		tests/update/reference_storage.c tests/update/reference_storage.h \
		src/update/confirmation.c src/update/transaction.c src/update/installer.c \
		$(UPDATE_STORAGE_SRCS) $(SECURITY_KEY_POLICY_SRCS) \
		src/security/protected_state.c $(SECURITY_PROVIDER_SRCS) \
		src/common/protocol.c products/validation/uefi-update-recovery/protocol.c \
		include/Ribon/update/confirmation.h $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(SECURITY_INCLUDE_FLAGS) \
		tests/update/confirmation_tests.c tests/update/reference_storage.c \
		src/update/confirmation.c src/update/transaction.c src/update/installer.c \
		$(UPDATE_STORAGE_SRCS) $(SECURITY_KEY_POLICY_SRCS) \
		src/security/protected_state.c $(SECURITY_PROVIDER_SRCS) \
		src/common/protocol.c products/validation/uefi-update-recovery/protocol.c \
		-o $@

$(BOOT_CONFIRMATION_SANITIZER_TEST): tests/update/confirmation_tests.c \
		tests/update/reference_storage.c tests/update/reference_storage.h \
		src/update/confirmation.c src/update/transaction.c src/update/installer.c \
		$(UPDATE_STORAGE_SRCS) $(SECURITY_KEY_POLICY_SRCS) \
		$(SECURITY_PROTECTED_STATE_SRCS) $(SECURITY_PROVIDER_SRCS) \
		src/common/protocol.c products/validation/uefi-update-recovery/protocol.c \
		include/Ribon/update/confirmation.h $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g $(WARNFLAGS) \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$(SECURITY_INCLUDE_FLAGS) tests/update/confirmation_tests.c \
		tests/update/reference_storage.c src/update/confirmation.c \
		src/update/transaction.c src/update/installer.c $(UPDATE_STORAGE_SRCS) \
		$(SECURITY_KEY_POLICY_SRCS) src/security/protected_state.c \
		$(SECURITY_PROVIDER_SRCS) src/common/protocol.c \
		products/validation/uefi-update-recovery/protocol.c -o $@

check-boot-confirmation-host: check-update-power-cut-host $(BOOT_CONFIRMATION_TEST)
	$(BOOT_CONFIRMATION_TEST) \
		$(UEFI_UPDATE_FIXTURE_DIR)/update.man \
		$(UEFI_UPDATE_FIXTURE_DIR)/layout.bin \
		$(UPDATE_POWER_CUT_CASES)/clean-pending.raw \
		$(UEFI_UPDATE_FIXTURE_DIR)/confirmation.bin \
		$(UEFI_UPDATE_MANIFEST)

check-boot-confirmation-sanitizer: check-update-power-cut-host \
		$(BOOT_CONFIRMATION_SANITIZER_TEST)
	$(BOOT_CONFIRMATION_SANITIZER_TEST) \
		$(UEFI_UPDATE_FIXTURE_DIR)/update.man \
		$(UEFI_UPDATE_FIXTURE_DIR)/layout.bin \
		$(UPDATE_POWER_CUT_CASES)/clean-pending.raw \
		$(UEFI_UPDATE_FIXTURE_DIR)/confirmation.bin \
		$(UEFI_UPDATE_MANIFEST)

check-boot-confirmation-cross-compile:
	@set -e; \
	for target in x86_64 aarch64 riscv64; do \
		case "$$target" in \
		x86_64) compiler=$(X86_64_CC); triple=x86_64-none-elf;; \
		aarch64) compiler=$(AARCH64_CC); triple=aarch64-none-elf;; \
		riscv64) compiler=$(RISCV64_CC); triple=riscv64-none-elf;; \
		esac; \
		object=$(BUILD_ROOT)/boot-confirmation/$$target/confirmation.o; \
		mkdir -p "$$(dirname "$$object")"; \
		"$$compiler" --target="$$triple" $(FREESTANDING_FLAGS) \
			-c src/update/confirmation.c -o "$$object"; \
	done
	@echo "RIBON-D06-BOOT-CONFIRMATION-CROSS-COMPILE-OK targets=x86_64,aarch64,riscv64"

check-qemu-boot-confirmation: x86_64-uefi-update-recovery
	$(PYTHON) tools/qemu_boot_confirmation.py \
		--qemu $(QEMU_X86_64) --firmware $(X86_64_UEFI_FIRMWARE) \
		--esp-template $(UEFI_UPDATE_ESP) \
		--disk-fixture $(UEFI_UPDATE_DISK) --boot-app $(UEFI_UPDATE_APP) \
		--manifest $(UEFI_UPDATE_FIXTURE_DIR)/update.man \
		--confirmation $(UEFI_UPDATE_FIXTURE_DIR)/confirmation.bin \
		--product-manifest $(UEFI_UPDATE_MANIFEST) \
		--fixture-provenance $(UEFI_UPDATE_FIXTURE_PROVENANCE) \
		--pending-inspector tools/inspect_qemu_update_transaction.py \
		--confirmed-inspector tools/inspect_qemu_boot_confirmation.py \
		--source-revision $(shell git rev-parse HEAD) \
		--results $(UEFI_UPDATE_RESULTS)/boot-confirmation

check-boot-confirmation-graphs: x86_64-uefi-update-recovery \
		x86_64-uefi-network-update-recovery
	$(PYTHON) tools/lint/boot_confirmation_core_lint.py --root $(ROOT) \
		--confirmation-graph $(UEFI_UPDATE_GRAPH) \
		--network-graph $(UEFI_NETWORK_UPDATE_GRAPH)

check-boot-confirmation: check-boot-confirmation-host \
		check-boot-confirmation-sanitizer \
		check-boot-confirmation-cross-compile \
		check-boot-confirmation-graphs check-qemu-boot-confirmation

$(UEFI_NETWORK_UPDATE_REGISTRY_C): \
	$(UEFI_NETWORK_UPDATE_MANIFEST) tools/generate_plugin_registry.py
	$(PYTHON) tools/generate_plugin_registry.py --manifest $< \
		--output $@ --report $(UEFI_NETWORK_UPDATE_GRAPH)

$(UEFI_NETWORK_UPDATE_GRAPH): $(UEFI_NETWORK_UPDATE_REGISTRY_C)
	@test -f $@

$(UEFI_NETWORK_UPDATE_FIXTURE_STAMP): \
	$(UEFI_NETWORK_UPDATE_MANIFEST) \
	tests/fixtures/update/update-layout-source-v1.json \
	tools/make_qemu_update_fixture.py tools/update_manifest.py tools/update_layout.py
	$(PYTHON) tools/make_qemu_update_fixture.py \
		--product-manifest $(UEFI_NETWORK_UPDATE_MANIFEST) \
		--layout-source tests/fixtures/update/update-layout-source-v1.json \
		--output-root $(UEFI_NETWORK_UPDATE_FIXTURE_DIR)
	@touch $@

$(UEFI_NETWORK_UPDATE_DIR)/obj/%.o: %.c $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(X86_64_CC) $(UEFI_FLAGS) $(SECURITY_INCLUDE_FLAGS) $(DEPFLAGS) -c $< -o $@

$(UEFI_NETWORK_UPDATE_DIR)/obj/generated/plugin_registry.o: \
	$(UEFI_NETWORK_UPDATE_REGISTRY_C)
	@mkdir -p $(@D)
	$(X86_64_CC) $(UEFI_FLAGS) $(SECURITY_INCLUDE_FLAGS) $(DEPFLAGS) -c $< -o $@

$(UEFI_NETWORK_UPDATE_APP): $(UEFI_NETWORK_UPDATE_OBJS)
	$(LLD_LINK) /Brepro /subsystem:efi_application /entry:efi_main /nodefaultlib \
		/machine:x64 /map:$(UEFI_NETWORK_UPDATE_DIR)/ribon.map /out:$@ \
		$(UEFI_NETWORK_UPDATE_OBJS)

$(UEFI_NETWORK_UPDATE_ESP)/EFI/BOOT/BOOTX64.EFI: $(UEFI_NETWORK_UPDATE_APP)
	@mkdir -p $(@D)
	cp $< $@

x86_64-uefi-network-update-recovery: \
	$(UEFI_NETWORK_UPDATE_ESP)/EFI/BOOT/BOOTX64.EFI \
	$(UEFI_NETWORK_UPDATE_FIXTURE_STAMP)

check-qemu-recovery-network-update: x86_64-uefi-network-update-recovery
	$(PYTHON) tools/qemu_recovery_network_update.py \
		--qemu $(QEMU_X86_64) --firmware $(X86_64_UEFI_FIRMWARE) \
		--esp $(UEFI_NETWORK_UPDATE_ESP) \
		--disk $(UEFI_NETWORK_UPDATE_DISK) \
		--boot-app $(UEFI_NETWORK_UPDATE_APP) \
		--tftp-root $(UEFI_NETWORK_UPDATE_FIXTURE_DIR) \
		--manifest $(UEFI_NETWORK_UPDATE_FIXTURE_DIR)/update.man \
		--envelope $(UEFI_NETWORK_UPDATE_FIXTURE_DIR)/update.sig \
		--bundle $(UEFI_NETWORK_UPDATE_FIXTURE_DIR)/update.bin \
		--product-manifest $(UEFI_NETWORK_UPDATE_MANIFEST) \
		--fixture-provenance $(UEFI_NETWORK_UPDATE_FIXTURE_PROVENANCE) \
		--inspector tools/inspect_qemu_update_transaction.py \
		--source-revision $(shell git rev-parse HEAD) \
		--results $(UEFI_NETWORK_UPDATE_RESULTS)

check-recovery-network-update: check-recovery-network-host \
		check-recovery-network-sanitizer \
		check-recovery-network-cross-compile \
		check-recovery-network-graphs check-update-manifest \
		check-update-installer check-update-power-cut-host \
		check-qemu-recovery-network-update

x86_64-uefi-parus-fixture-smoke: x86_64-uefi-parus-fixture
	$(PYTHON) tools/qemu_target_smoke.py \
		--target x86_64-uefi --qemu $(QEMU_X86_64) \
		--esp $(UEFI_FIXTURE_ESP) --firmware $(X86_64_UEFI_FIRMWARE) \
		--payload $(UEFI_FIXTURE_PAYLOAD) --expected-payload-class fixture \
		--init-image $(UEFI_FIXTURE_INIT_IMAGE) \
		--product-manifest $(UEFI_FIXTURE_MANIFEST) \
		--source-revision $(shell git rev-parse HEAD) \
		--log $(UEFI_FIXTURE_DIR)/results/qemu.log \
		--result $(UEFI_FIXTURE_DIR)/results/qemu.json

x86_64-uefi-parus-external-smoke:
	@test -n "$(UEFI_PARUS_PAYLOAD)" || \
		{ echo "UEFI_PARUS_PAYLOAD is required" >&2; exit 2; }
	$(MAKE) --no-print-directory \
		UEFI_PARUS_PAYLOAD=$(abspath $(UEFI_PARUS_PAYLOAD)) \
		x86_64-uefi-parus-external-product
	$(PYTHON) tools/qemu_target_smoke.py \
		--target x86_64-uefi --qemu $(QEMU_X86_64) \
		--esp $(UEFI_EXTERNAL_ESP) --firmware $(X86_64_UEFI_FIRMWARE) \
		--payload $(UEFI_EXTERNAL_PAYLOAD) --expected-payload-class kernel \
		--init-image $(UEFI_EXTERNAL_INIT_IMAGE) \
		--product-manifest $(UEFI_EXTERNAL_MANIFEST) \
		$(PARUS_SUCCESS_MARKER_ARGS) \
		--source-revision $(shell git rev-parse HEAD) \
		--log $(UEFI_EXTERNAL_DIR)/results/qemu.log \
		--result $(UEFI_EXTERNAL_DIR)/results/qemu.json

x86_64-uefi-linux-smoke: x86_64-uefi-linux-product
	$(PYTHON) tools/qemu_target_smoke.py \
		--target x86_64-uefi-managed --qemu $(QEMU_X86_64) \
		--esp $(UEFI_LINUX_ESP) --firmware $(X86_64_UEFI_FIRMWARE) \
		--payload $(UEFI_LINUX_PAYLOAD) --expected-payload-class linux-efi \
		--expected-payload-sha256 2a0deaeab7dd3edf23c68597e1c79e0bd0f1ad92381cc90b3abd0187e96f28fe \
		--init-image $(UEFI_LINUX_INITRAMFS) \
		--product-manifest $(UEFI_LINUX_MANIFEST) \
		--external-payload-validation $(UEFI_LINUX_EXTERNAL_VALIDATION) \
		--expect-clean-exit --timeout 60 \
		--required-marker-anywhere RIBON:LINUX:X86_64:PID1:v1:OK \
		--required-marker-anywhere 'reboot: Power down' \
		--source-revision $(shell git rev-parse HEAD) \
		--log $(UEFI_LINUX_DIR)/results/qemu.log \
		--result $(UEFI_LINUX_DIR)/results/qemu.json

x86_64-uefi-freebsd-smoke: x86_64-uefi-freebsd-product
	$(PYTHON) tools/qemu_target_smoke.py \
		--target x86_64-uefi-freebsd --qemu $(QEMU_X86_64) \
		--disk-image $(UEFI_FREEBSD_DISK) --firmware $(X86_64_UEFI_FIRMWARE) \
		--payload $(UEFI_FREEBSD_LOADER) --expected-payload-class freebsd-efi \
		--product-manifest $(UEFI_FREEBSD_MANIFEST) \
		--external-payload-validation $(UEFI_FREEBSD_EXTERNAL_VALIDATION) \
		--package-provenance $(UEFI_FREEBSD_PACKAGE) --timeout 120 \
		--required-marker-anywhere 'FreeBSD 15.1-RELEASE' \
		--required-marker-anywhere 'Enter full pathname of shell or RETURN for /bin/sh:' \
		--source-revision $(shell git rev-parse HEAD) \
		--log $(UEFI_FREEBSD_DIR)/results/qemu.log \
		--result $(UEFI_FREEBSD_DIR)/results/qemu.json

check-terminal-image-launch: check-boot-lifecycle check-core-service
	@echo "RIBON-R02-TERMINAL-IMAGE-LAUNCH-OK"

check-uefi-managed-image: x86_64-uefi-linux-product \
		x86_64-uefi-freebsd-product x86_64-uefi-parus-fixture
	@! grep -q ribon_uefi_app_terminal_image_launch_service_descriptor \
		$(UEFI_FIXTURE_DIR)/ribon.map
	@grep -q ribon_uefi_app_terminal_image_launch_service_descriptor \
		$(UEFI_LINUX_DIR)/ribon.map
	@grep -q ribon_uefi_app_terminal_image_launch_service_descriptor \
		$(UEFI_FREEBSD_DIR)/ribon.map
	@echo "RIBON-R02-UEFI-MANAGED-IMAGE-OK"

check-linux-x86_64-efi: x86_64-uefi-linux-smoke
	$(PYTHON) tests/tools/external_linux_efi_tests.py
	@echo "RIBON-R02-LINUX-X86_64-EFI-OK"

check-freebsd-package: x86_64-uefi-freebsd-product
	$(PYTHON) tests/tools/external_freebsd_tests.py \
		--product $(UEFI_FREEBSD_DIR)
	@echo "RIBON-R03-FREEBSD-PACKAGE-OK"

check-freebsd-uefi: check-freebsd-package check-os-packages \
		x86_64-uefi-freebsd-smoke
	@grep -q ribon_uefi_app_terminal_image_launch_service_descriptor \
		$(UEFI_FREEBSD_DIR)/ribon.map
	@grep -q ribon_freebsd_protocol_plugin_descriptor \
		$(UEFI_FREEBSD_DIR)/ribon.map
	@echo "RIBON-R03-FREEBSD-UEFI-OK"

$(BIOS_REGISTRY_C): $(BIOS_MANIFEST) tools/generate_plugin_registry.py
	$(PYTHON) tools/generate_plugin_registry.py --manifest $< \
		--output $@ --report $(BIOS_GRAPH)

$(BIOS_DIR)/obj/%.o: %.c
	@mkdir -p $(@D)
	$(X86_64_CC) $(BIOS_FLAGS) $(DEPFLAGS) -c $< -o $@

bios-compile: $(BIOS_REGISTRY_C) $(BIOS_OBJECTS)
	@echo "RIBON-R4-BIOS-COMPILE-ONLY-OK"
