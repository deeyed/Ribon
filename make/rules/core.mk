.PHONY: all lib sdk-install host-reference check check-one check-loader check-pe-coff \
	check-fdt check-rlh1 check-arch-x86_64 check-arch-aarch64 \
	check-arch-ops check-core-service check-port-services check-boot-lifecycle \
	check-environment-persistent-inputs check-boot-modules check-media-pipeline \
	check-mode-descriptors check-plugin-descriptors check-protocol-contract \
	check-luca-entry-contract \
	check-os-packages \
	check-linux-boot \
	check-linux-external-input \
	check-library-embed check-object-graphs check-public-api \
	check-composition-schemas check-sdk-surface check-sdk-embed \
	check-ribos-extension-sdk \
	check-qemu-evidence \
	check-uefi-product-hermeticity \
	check-sdk-reproducible check-external-plugin check-firmware-personalities \
	check-sdk-deployment-consumer check-rpi5-prehardware \
	deployment-release-artifacts check-deployment-release-reproducibility \
	check-ribon-deployment-closure \
	check-firmware-object-graphs firmware-provider-reference \
	check-frontends check-normal-media-surface check-target-builds qemu-aarch64-virt-raw-fdt \
	ribosc ribos-verify ribos-run ribos-parser-pilot \
	check-ribos-parser-snapshot check-ribos-parser-pilot \
	check-ribos-semantics check-ribos-schema check-ribos-ir \
	check-ribos-resources check-ribos-artifact check-ribos-verifier \
	check-ribos-runtime-contract check-ribos-prepared-program \
	check-ribos-runtime-storage check-ribos-vm-scalar \
	check-ribos-vm-calls check-ribos-vm-loops \
	check-ribos-vm-aggregates check-ribos-vm-handles \
	check-ribos-vm-helpers check-ribos-vm-terminal \
	check-ribos-vm-faults check-ribos-host-tools \
	check-ribos-replay check-ribos-conformance \
	check-ribos-hostile check-ribos-executable-corpus check-ribos-vm \
	check-ribos-ribon-integration check-ribos-product-graphs \
	check-ribos-normal-no-network check-ribos-factory-recovery \
	check-ribos-host-boundary check-ribos-golden-artifact \
	check-ribos-cross-arch-objects check-ribos-cross-arch-qemu \
	check-ribos-r18 ribos-r18-release-artifacts \
	check-ribos-release-reproducibility \
	check-ribos-production-policy ribos-libraries \
	check-security-ed25519-provider check-security-ed25519-sanitizer \
	check-security-ed25519-cross-compile check-security-provider-graphs \
	check-security-key-policy check-security-key-policy-sanitizer \
	check-security-key-policy-graphs \
	check-update-manifest check-update-manifest-sanitizer \
	check-update-manifest-cross-compile \
	check-update-storage check-update-storage-sanitizer \
	check-update-storage-cross-compile check-update-storage-graphs \
	check-update-installer check-uefi-update-storage check-qemu-update-install \
	check-update-power-cut check-update-power-cut-host \
	check-update-power-cut-sanitizer check-update-transaction-cross-compile \
	check-qemu-update-power-cut check-boot-confirmation \
	check-boot-confirmation-host check-boot-confirmation-sanitizer \
	check-boot-confirmation-cross-compile check-boot-confirmation-graphs \
	check-qemu-boot-confirmation check-recovery-network-host \
	check-recovery-network-sanitizer check-recovery-network-cross-compile \
	check-recovery-network-graphs check-qemu-recovery-network-update \
	check-recovery-network-update x86_64-uefi-network-update-recovery \
	ribos-parser-generate ribos-parser-regenerate-check \
	qemu-aarch64-virt-raw-fdt-smoke qemu-aarch64-virt-parus-product \
	qemu-aarch64-virt-linux-product qemu-aarch64-virt-linux-smoke \
	qemu-aarch64-virt-parus-smoke \
	qemu-aarch64-virt-modules-fixture-product \
	qemu-aarch64-virt-modules-fixture-smoke \
	qemu-aarch64-virt-parus-modules-product \
	qemu-aarch64-virt-parus-modules-smoke x86_64-uefi-parus-fixture \
	qemu-riscv64-virt-parus-product qemu-riscv64-virt-parus-smoke \
	qemu-riscv64-virt-rlh1-fixture-product \
	qemu-riscv64-virt-rlh1-fixture-smoke \
	qemu-riscv64-virt-linux-product qemu-riscv64-virt-linux-smoke \
	x86_64-uefi-parus-external x86_64-uefi-parus-external-product \
	x86_64-uefi-parus-fixture-smoke x86_64-uefi-parus-external-smoke \
	x86_64-uefi-linux-product x86_64-uefi-linux-smoke \
	check-terminal-image-launch check-uefi-managed-image check-linux-x86_64-efi \
	check-linux-riscv64 check-multi-os-runtime \
	x86_64-uefi-freebsd-product x86_64-uefi-freebsd-smoke \
	check-freebsd-package check-freebsd-uefi \
	uefi-external-input-force rpi5-external-input-force \
	bios-compile rpi5-aarch64-raw-fdt-package \
	rpi5-aarch64-parus-package rpi5-aarch64-modules-fixture-package \
	rpi5-aarch64-parus-modules-package \
	boot-module-input-force qemu-module-input-force rpi5-module-input-force \
	legacy-hard-cut qstar-check docs docs-lint docs-clean clean

all: lib host-reference

lib: $(CORE_LIB) $(BOOT_LIB) $(SDK_LIB) $(SDK_UPDATE_LIB) $(RIBOS_POLICY_LIB) \
	$(RIBOS_TARGET_CORE_LIB)

host-reference: $(HOST_REFERENCE)

define RIBOS_TARGET_OBJECT
$(RIBOS_OBJECT_DIR)/target/$(1).o: $(2) $(RIBOS_HEADERS) $(RIBON_MAKEFILES)
	@mkdir -p $$(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) -ffreestanding -fno-builtin \
		$(RIBOS_TARGET_INCLUDE_FLAGS) -c $$< -o $$@
endef

define RIBOS_HOST_SUPPORT_OBJECT
$(RIBOS_OBJECT_DIR)/host-support/$(1).o: $(2) $(RIBOS_HEADERS) $(RIBON_MAKEFILES)
	@mkdir -p $$(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) -c $$< -o $$@
endef

define RIBOS_HOST_COMPILER_OBJECT
$(RIBOS_OBJECT_DIR)/host-compiler/$(1).o: $(2) $(RIBOS_HEADERS) $(RIBON_MAKEFILES)
	@mkdir -p $$(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) -c $$< -o $$@
endef

$(eval $(call RIBOS_TARGET_OBJECT,base_allocator,language/ribos/base/src/allocator.c))
$(eval $(call RIBOS_TARGET_OBJECT,base_checked,language/ribos/base/src/checked.c))
$(eval $(call RIBOS_TARGET_OBJECT,base_writer,language/ribos/base/src/writer.c))
$(eval $(call RIBOS_TARGET_OBJECT,schema,language/ribos/schema/src/schema.c))
$(eval $(call RIBOS_TARGET_OBJECT,artifact_wire,language/ribos/artifact/src/wire.c))
$(eval $(call RIBOS_TARGET_OBJECT,artifact_sha256,language/ribos/artifact/src/sha256.c))
$(eval $(call RIBOS_TARGET_OBJECT,artifact_codec,language/ribos/artifact/src/codec.c))
$(eval $(call RIBOS_TARGET_OBJECT,verifier,language/ribos/vm/src/verifier.c))
$(eval $(call RIBOS_TARGET_OBJECT,prepared,language/ribos/vm/src/prepared.c))
$(eval $(call RIBOS_TARGET_OBJECT,runtime_storage,language/ribos/vm/src/runtime/storage.c))
$(eval $(call RIBOS_TARGET_OBJECT,runtime_handles,language/ribos/vm/src/runtime/handles.c))
$(eval $(call RIBOS_TARGET_OBJECT,runtime_helpers,language/ribos/vm/src/runtime/helpers.c))
$(eval $(call RIBOS_TARGET_OBJECT,runtime_interpreter,language/ribos/vm/src/runtime/interpreter.c))
$(eval $(call RIBOS_TARGET_OBJECT,runtime_terminal,language/ribos/vm/src/runtime/terminal.c))

$(BUILD_DIR)/obj/src/plugins/policy/ribos/adapter.o: \
		src/plugins/policy/ribos/adapter.c \
		include/Ribon/policy/ribos.h include/Ribon/security/key_policy.h \
		include/Ribon/security/protected_state.h $(RIBOS_HEADERS) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(DEPFLAGS) \
		$(RIBOS_TARGET_INCLUDE_FLAGS) -c $< -o $@

$(BUILD_DIR)/obj/src/plugins/policy/ribos/extension.o: \
		src/plugins/policy/ribos/extension.c \
		include/Ribon/policy/ribos_extension.h $(RIBOS_HEADERS) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(DEPFLAGS) \
		$(RIBOS_TARGET_INCLUDE_FLAGS) -c $< -o $@

$(HOST_SECURITY_OBJS): CPPFLAGS += $(SECURITY_INCLUDE_FLAGS)
$(SDK_UPDATE_OBJS): CPPFLAGS += $(SECURITY_INCLUDE_FLAGS)

$(BUILD_DIR)/obj/src/environments/host/ribos_policy.o: \
		src/environments/host/ribos_policy.c \
		src/environments/host/ribos_policy.h $(RIBOS_HEADERS) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(DEPFLAGS) \
		$(RIBOS_TARGET_INCLUDE_FLAGS) -c $< -o $@

$(eval $(call RIBOS_HOST_SUPPORT_OBJECT,allocator,language/ribos/host/src/allocator.c))
$(eval $(call RIBOS_HOST_SUPPORT_OBJECT,format,language/ribos/host/src/format.c))
$(eval $(call RIBOS_HOST_SUPPORT_OBJECT,writer,language/ribos/host/src/writer.c))

$(eval $(call RIBOS_HOST_COMPILER_OBJECT,ir_module,language/ribos/ir/src/module.c))
$(eval $(call RIBOS_HOST_COMPILER_OBJECT,ir_dump,language/ribos/ir/src/dump.c))
$(eval $(call RIBOS_HOST_COMPILER_OBJECT,ir_analysis,language/ribos/ir/src/analysis.c))
$(eval $(call RIBOS_HOST_COMPILER_OBJECT,artifact_emitter,language/ribos/host/src/artifact_emitter.c))
$(eval $(call RIBOS_HOST_COMPILER_OBJECT,lexer,language/ribos/frontend/src/lexer.c))
$(eval $(call RIBOS_HOST_COMPILER_OBJECT,runtime,language/ribos/frontend/src/runtime.c))
$(eval $(call RIBOS_HOST_COMPILER_OBJECT,ast,language/ribos/frontend/src/ast.c))
$(eval $(call RIBOS_HOST_COMPILER_OBJECT,parser,language/ribos/frontend/src/parser.c))
$(eval $(call RIBOS_HOST_COMPILER_OBJECT,compiler,language/ribos/frontend/src/compiler.c))
$(eval $(call RIBOS_HOST_COMPILER_OBJECT,semantic,language/ribos/frontend/src/semantic.c))
$(eval $(call RIBOS_HOST_COMPILER_OBJECT,lower,language/ribos/frontend/src/lower.c))
$(eval $(call RIBOS_HOST_COMPILER_OBJECT,frontend_dump,language/ribos/frontend/src/dump.c))
$(eval $(call RIBOS_HOST_COMPILER_OBJECT,generated_parser,language/ribos/frontend/generated/parser.c))

$(RIBOS_TARGET_CORE_LIB): $(RIBOS_TARGET_CORE_OBJS) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(RM) $@
	$(AR) rcs $@ $(RIBOS_TARGET_CORE_OBJS)

$(RIBOS_POLICY_LIB): $(RIBOS_POLICY_OBJS) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(RM) $@
	$(AR) rcs $@ $(RIBOS_POLICY_OBJS)

$(RIBOS_HOST_SUPPORT_LIB): $(RIBOS_HOST_SUPPORT_OBJS) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(RM) $@
	$(AR) rcs $@ $(RIBOS_HOST_SUPPORT_OBJS)

$(RIBOS_HOST_COMPILER_LIB): $(RIBOS_HOST_COMPILER_OBJS) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(RM) $@
	$(AR) rcs $@ $(RIBOS_HOST_COMPILER_OBJS)

ribos-libraries: $(RIBOS_TARGET_CORE_LIB) $(RIBOS_HOST_SUPPORT_LIB) \
		$(RIBOS_HOST_COMPILER_LIB)

$(RIBOS_OBJECT_DIR)/tools/parse.o: language/ribos/host/tools/parse.c \
		$(RIBOS_HEADERS) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) -c $< -o $@

$(RIBOS_PARSER_PILOT): $(RIBOS_OBJECT_DIR)/tools/parse.o \
		$(RIBOS_HOST_COMPILER_LIB) $(RIBOS_TARGET_CORE_LIB) \
		$(RIBOS_HOST_SUPPORT_LIB) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) \
		$(RIBOS_OBJECT_DIR)/tools/parse.o \
		$(RIBOS_HOST_COMPILER_LIB) $(RIBOS_TARGET_CORE_LIB) \
		$(RIBOS_HOST_SUPPORT_LIB) -o $@

ribosc: $(RIBOS_PARSER_PILOT)

ribos-parser-pilot: $(RIBOS_PARSER_PILOT)
