legacy-hard-cut:
	$(PYTHON) tools/lint/legacy_os_hard_cut.py
	$(PYTHON) tools/lint/library_plugin_hard_cut.py
	$(PYTHON) tools/lint/monolithic_service_hard_cut.py

check-public-api:
	$(PYTHON) tools/lint/public_api_layout_lint.py

check-frontends:
	$(PYTHON) tools/lint/frontend_boundary_lint.py

check-loader: $(LOADER_TEST)
	$(LOADER_TEST)

check-pe-coff: $(PE_COFF_TEST)
	$(PE_COFF_TEST)

check-fdt: $(FDT_TEST)
	$(FDT_TEST)

check-rph1: $(RPH1_TEST)
	$(RPH1_TEST)

check-arch-x86_64: $(ARCH_X86_64_TEST)
	$(ARCH_X86_64_TEST)

check-arch-aarch64: $(ARCH_AARCH64_TEST)
	$(ARCH_AARCH64_TEST)

check-arch-ops: $(ARCH_OPS_TESTS)
	@for test_binary in $(ARCH_OPS_TESTS); do \
		$$test_binary || exit $$?; \
	done

check-core-service: $(CORE_SERVICE_TEST)
	$(CORE_SERVICE_TEST)

check-port-services: $(PORT_SERVICE_TEST)
	$(PORT_SERVICE_TEST)

check-boot-lifecycle: $(BOOT_LIFECYCLE_TEST)
	$(BOOT_LIFECYCLE_TEST)

check-environment-persistent-inputs: $(ENVIRONMENT_PERSISTENT_INPUTS_TEST)
	$(ENVIRONMENT_PERSISTENT_INPUTS_TEST)

check-boot-modules: $(BOOT_MODULE_BUNDLE_TEST) $(RAW_FDT_CAPACITY_TEST)
	$(BOOT_MODULE_BUNDLE_TEST)
	$(RAW_FDT_CAPACITY_TEST)
	$(PYTHON) tests/tools/boot_module_bundle_tests.py
	$(PYTHON) tests/tools/rpi_package_tests.py

check-media-pipeline: $(MEDIA_PIPELINE_TEST)
	$(MEDIA_PIPELINE_TEST) --fuzz-smoke

check-normal-media-surface: x86_64-uefi-parus-fixture
	$(PYTHON) tools/lint/normal_media_surface_lint.py \
		$(UEFI_FIXTURE_DIR)/ribon.map

check-mode-descriptors: $(MODE_DESCRIPTOR_TESTS)
	@for test_binary in $(MODE_DESCRIPTOR_TESTS); do \
		$$test_binary || exit $$?; \
	done

check-plugin-descriptors: $(PLUGIN_DESCRIPTOR_TEST)
	$(PLUGIN_DESCRIPTOR_TEST)

check-protocol-contract: $(PROTOCOL_CONTRACT_TEST)
	$(PROTOCOL_CONTRACT_TEST)

check-parus-entry-contract: $(PARUS_ENTRY_CONTRACT_TEST)
	$(PARUS_ENTRY_CONTRACT_TEST)

check-os-packages: $(OS_PACKAGE_TEST)
	$(OS_PACKAGE_TEST)

check-linux-boot: $(LINUX_BOOT_TEST)
	$(LINUX_BOOT_TEST)

check-linux-external-input:
	$(PYTHON) tests/tools/external_linux_image_tests.py
	$(PYTHON) tests/tools/external_linux_riscv64_image_tests.py

check-library-embed: $(PROTOCOL_FREE_EMBED_TEST)
	$(PROTOCOL_FREE_EMBED_TEST)

check-composition-schemas:
	$(PYTHON) tools/lint/composition_schema_lint.py

check-qemu-evidence:
	$(PYTHON) tests/tools/qemu_target_smoke_tests.py
	$(PYTHON) tests/tools/external_parus_payload_tests.py
	$(PYTHON) tests/tools/external_linux_image_tests.py
	$(PYTHON) tests/tools/external_linux_riscv64_image_tests.py
	$(PYTHON) tests/tools/multi_os_runtime_tests.py

sdk-install: lib ribosc ribos-verify ribos-run
	$(PYTHON) tools/install_sdk.py \
		--root $(SDK_INSTALL_ROOT) \
		--public-include include/Ribon \
		--library $(CORE_LIB) \
		--library $(BOOT_LIB) \
		--library $(SDK_LIB) \
		--library $(SDK_UPDATE_LIB) \
		--host-tool ribosc=$(RIBOS_PARSER_PILOT) \
		--host-tool ribos-verify=$(RIBOS_VERIFIER) \
		--host-tool ribos-run=$(RIBOS_RUNNER) \
		--host-tool ribon-compose-product=tools/generate_plugin_registry.py \
		--host-tool ribon-update-manifest=tools/update_manifest.py \
		--host-tool ribon-update-layout=tools/update_layout.py \
		--host-tool ribon-sign-policy=tools/sign_ribos_policy.py \
		--schemas qstar/schemas \
		--templates sdk/templates \
		--source-revision $(shell git rev-parse HEAD) \
		--sdk-abi 6 --core-abi 7 \
		--plugin-abi-major 6 --plugin-abi-minor 0 \
		--source-version 0.4.0

check-sdk-surface: sdk-install
	$(PYTHON) tools/lint/sdk_surface_lint.py \
		--install-root $(SDK_INSTALL_ROOT)

$(SDK_LIBRARY_EMBED_TEST): sdk-install examples/library-embed/main.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) \
		-I$(SDK_INSTALL_ROOT)/include \
		examples/library-embed/main.c \
		$(SDK_INSTALL_ROOT)/lib/libribon-sdk.a \
		$(SDK_INSTALL_ROOT)/lib/libribon-boot.a \
		$(SDK_INSTALL_ROOT)/lib/libribon-core.a \
		-o $@

check-sdk-embed: $(SDK_LIBRARY_EMBED_TEST)
	$(SDK_LIBRARY_EMBED_TEST)

check-sdk-reproducible: lib ribosc ribos-verify ribos-run
	$(PYTHON) tools/install_sdk.py \
		--root $(SDK_REPRO_FIRST) \
		--public-include include/Ribon \
		--library $(CORE_LIB) --library $(BOOT_LIB) --library $(SDK_LIB) \
		--library $(SDK_UPDATE_LIB) \
		--host-tool ribosc=$(RIBOS_PARSER_PILOT) \
		--host-tool ribos-verify=$(RIBOS_VERIFIER) \
		--host-tool ribos-run=$(RIBOS_RUNNER) \
		--host-tool ribon-compose-product=tools/generate_plugin_registry.py \
		--host-tool ribon-update-manifest=tools/update_manifest.py \
		--host-tool ribon-update-layout=tools/update_layout.py \
		--host-tool ribon-sign-policy=tools/sign_ribos_policy.py \
		--schemas qstar/schemas --templates sdk/templates \
		--source-revision $(shell git rev-parse HEAD) \
		--sdk-abi 6 --core-abi 7 --plugin-abi-major 6 \
		--plugin-abi-minor 0 --source-version 0.4.0
	$(PYTHON) tools/install_sdk.py \
		--root $(SDK_REPRO_SECOND) \
		--public-include include/Ribon \
		--library $(CORE_LIB) --library $(BOOT_LIB) --library $(SDK_LIB) \
		--library $(SDK_UPDATE_LIB) \
		--host-tool ribosc=$(RIBOS_PARSER_PILOT) \
		--host-tool ribos-verify=$(RIBOS_VERIFIER) \
		--host-tool ribos-run=$(RIBOS_RUNNER) \
		--host-tool ribon-compose-product=tools/generate_plugin_registry.py \
		--host-tool ribon-update-manifest=tools/update_manifest.py \
		--host-tool ribon-update-layout=tools/update_layout.py \
		--host-tool ribon-sign-policy=tools/sign_ribos_policy.py \
		--schemas qstar/schemas --templates sdk/templates \
		--source-revision $(shell git rev-parse HEAD) \
		--sdk-abi 6 --core-abi 7 --plugin-abi-major 6 \
		--plugin-abi-minor 0 --source-version 0.4.0
	$(PYTHON) tools/check_reproducible_trees.py \
		$(SDK_REPRO_FIRST) $(SDK_REPRO_SECOND)

$(EXTERNAL_PLUGIN_REGISTRY_C): \
	$(EXTERNAL_PLUGIN_ROOT)/tests/product.json \
	tools/generate_plugin_registry.py
	$(PYTHON) tools/generate_plugin_registry.py \
		--manifest $< --output $@ --report $(EXTERNAL_PLUGIN_REPORT)

$(EXTERNAL_PLUGIN_TEST): sdk-install $(EXTERNAL_PLUGIN_REGISTRY_C) \
	$(EXTERNAL_PLUGIN_ROOT)/src/plugin.c \
	$(EXTERNAL_PLUGIN_ROOT)/tests/fixture_providers.c \
	$(EXTERNAL_PLUGIN_ROOT)/tests/contract.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) \
		-I$(SDK_INSTALL_ROOT)/include \
		-I$(EXTERNAL_PLUGIN_ROOT)/include \
		$(EXTERNAL_PLUGIN_ROOT)/src/plugin.c \
		$(EXTERNAL_PLUGIN_ROOT)/tests/fixture_providers.c \
		$(EXTERNAL_PLUGIN_ROOT)/tests/contract.c \
		$(EXTERNAL_PLUGIN_REGISTRY_C) \
		$(SDK_INSTALL_ROOT)/lib/libribon-sdk.a \
		$(SDK_INSTALL_ROOT)/lib/libribon-boot.a \
		$(SDK_INSTALL_ROOT)/lib/libribon-core.a \
		-o $@

check-external-plugin: $(EXTERNAL_PLUGIN_TEST)
	$(EXTERNAL_PLUGIN_TEST)

check-sdk-deployment-consumer: sdk-install
	$(PYTHON) tools/check_sdk_deployment_consumer.py \
		--install-root $(SDK_INSTALL_ROOT) \
		--work-root $(SDK_DEPLOYMENT_CONSUMER_DIR) \
		--cc $(CC)

check-rpi5-prehardware: rpi5-aarch64-modules-fixture-package
	$(PYTHON) tools/make_rpi5_prehardware_update.py \
		--package $(RPI5_MODULE_FIXTURE_DIR)/package \
		--private-seed tests/fixtures/security/rfc8032-test1-seed.hex \
		--source-revision $(shell git rev-parse HEAD) \
		--output-root $(BUILD_ROOT)/release/rpi5-prehardware

deployment-release-artifacts: check-sdk-surface check-sdk-deployment-consumer \
		check-rpi5-prehardware
	$(PYTHON) tools/make_deployment_release_manifest.py \
		--source-revision $(shell git rev-parse HEAD) \
		--cc $(CC) --sdk-root $(SDK_INSTALL_ROOT) \
		--consumer-report $(SDK_DEPLOYMENT_CONSUMER_REPORT) \
		--rpi5-package $(RPI5_MODULE_FIXTURE_DIR)/package \
		--rpi5-prehardware $(BUILD_ROOT)/release/rpi5-prehardware/prehardware.json \
		--output $(BUILD_ROOT)/release/deployment-release.json

check-deployment-release-reproducibility:
	$(PYTHON) tools/check_deployment_release_reproducibility.py \
		--make $(MAKE) --root $(ROOT) \
		--work-root $(BUILD_ROOT)/tests/deployment-release-reproducibility

check-ribon-deployment-closure: check-update-manifest check-update-storage \
		check-qemu-update-install check-update-power-cut \
		check-recovery-network-update check-boot-confirmation \
		qemu-aarch64-virt-linux-smoke deployment-release-artifacts \
		check-deployment-release-reproducibility
	@echo "RIBON-D08-DEPLOYMENT-CLOSURE-OK sdk=installed update=transactional \
network=recovery-only confirmation=os-neutral linux=aarch64-qemu \
rpi5=prehardware hardware=not-run"

$(UEFI_PROVIDER_REGISTRY_C): $(UEFI_PROVIDER_MANIFEST) \
	tools/generate_plugin_registry.py
	$(PYTHON) tools/generate_plugin_registry.py \
		--manifest $< --output $@ --report $(UEFI_PROVIDER_REPORT)

$(BIOS_PROVIDER_REGISTRY_C): $(BIOS_PROVIDER_MANIFEST) \
	tools/generate_plugin_registry.py
	$(PYTHON) tools/generate_plugin_registry.py \
		--manifest $< --output $@ --report $(BIOS_PROVIDER_REPORT)

$(UEFI_PROVIDER_DIR)/obj/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(DEPFLAGS) -c $< -o $@

$(BIOS_PROVIDER_DIR)/obj/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(DEPFLAGS) -c $< -o $@

$(UEFI_PROVIDER_DIR)/obj/generated/plugin_registry.o: $(UEFI_PROVIDER_REGISTRY_C)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(DEPFLAGS) -c $< -o $@

$(BIOS_PROVIDER_DIR)/obj/generated/plugin_registry.o: $(BIOS_PROVIDER_REGISTRY_C)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(DEPFLAGS) -c $< -o $@

$(UEFI_PROVIDER_BIN): $(UEFI_PROVIDER_OBJS) $(SDK_LIB) $(BOOT_LIB) $(CORE_LIB)
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(BIOS_PROVIDER_BIN): $(BIOS_PROVIDER_OBJS) $(SDK_LIB) $(BOOT_LIB) $(CORE_LIB)
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

firmware-provider-reference: $(UEFI_PROVIDER_BIN) $(BIOS_PROVIDER_BIN)

check-firmware-personalities: firmware-provider-reference
	$(UEFI_PROVIDER_BIN)
	$(BIOS_PROVIDER_BIN)

check-firmware-object-graphs: firmware-provider-reference
	$(PYTHON) tools/lint/firmware_object_graph_lint.py \
		--makefile $(RIBON_MODEL_MAKEFILE) \
		--provider-root $(FIRMWARE_PROVIDER_ROOT)

check-object-graphs: $(CORE_LIB) $(BOOT_LIB) $(SDK_LIB) $(RIBOS_POLICY_LIB) \
		$(RIBOS_TARGET_CORE_LIB)
	$(PYTHON) tools/lint/object_graph_lint.py \
		--core $(CORE_LIB) --boot $(BOOT_LIB) --sdk $(SDK_LIB)
	$(PYTHON) tools/lint/ribos_object_graph_lint.py \
		--adapter $(RIBOS_POLICY_LIB) \
		--vm-core $(RIBOS_TARGET_CORE_LIB)

qstar-check:
	$(QSTAR) --file qstar.lua check
