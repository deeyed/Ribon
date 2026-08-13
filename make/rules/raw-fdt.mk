boot-module-input-force:

ifneq ($(strip $(QEMU_RAW_MODULE_COMPONENT_MANIFEST)),)
$(QEMU_RAW_MODULE_STAMP): \
	$(QEMU_RAW_MODULE_COMPONENT_MANIFEST) \
	$(QEMU_RAW_MANIFEST) \
	tools/generate_boot_module_bundle.py boot-module-input-force
	$(PYTHON) tools/generate_boot_module_bundle.py \
		--manifest $(QEMU_RAW_MODULE_COMPONENT_MANIFEST) \
		--product-manifest $(QEMU_RAW_MANIFEST) \
		--output-root $(QEMU_RAW_DIR) \
		--assembly $(QEMU_RAW_MODULE_ASM) \
		--descriptors $(QEMU_RAW_MODULE_C) \
		--provenance $(QEMU_RAW_MODULE_PROVENANCE)
	@touch $@

$(QEMU_RAW_MODULE_ASM) $(QEMU_RAW_MODULE_C) \
		$(QEMU_RAW_MODULE_PROVENANCE): $(QEMU_RAW_MODULE_STAMP)
	@test -f $@

$(QEMU_RAW_DIR)/obj/generated/boot-modules/descriptor.o: \
	$(QEMU_RAW_MODULE_C)
	@mkdir -p $(@D)
	$(AARCH64_CC) $(AARCH64_FLAGS) $(DEPFLAGS) -c $< -o $@

$(QEMU_RAW_DIR)/obj/generated/boot-modules/bundle.o: \
	$(QEMU_RAW_MODULE_ASM)
	@mkdir -p $(@D)
	$(AARCH64_CC) --target=aarch64-none-elf \
		-I$(QEMU_RAW_MODULE_DIR) -c $< -o $@
endif

$(QEMU_RAW_DIR)/obj/%.o: %.c
	@mkdir -p $(@D)
	$(AARCH64_CC) $(AARCH64_FLAGS) $(QEMU_RAW_CPPFLAGS) \
		$(QEMU_RAW_MODULE_CPPFLAGS) \
		$(DEPFLAGS) -c $< -o $@

$(QEMU_RAW_DIR)/obj/generated/plugin_registry.o: $(QEMU_RAW_REGISTRY_C)
	@mkdir -p $(@D)
	$(AARCH64_CC) $(AARCH64_FLAGS) $(DEPFLAGS) -c $< -o $@

$(QEMU_RAW_DIR)/obj/generated/embedded_payload.o: $(QEMU_RAW_EMBED_C)
	@mkdir -p $(@D)
	$(AARCH64_CC) $(AARCH64_FLAGS) $(DEPFLAGS) -c $< -o $@

ifneq ($(strip $(QEMU_RAW_EXTERNAL_PAYLOAD_ASM)),)
$(QEMU_RAW_DIR)/obj/generated/external_payload.o: $(QEMU_RAW_EXTERNAL_PAYLOAD_ASM)
	@mkdir -p $(@D)
	$(AARCH64_CC) --target=aarch64-none-elf -c $< -o $@
endif

$(QEMU_RAW_DIR)/obj/targets/qemu-aarch64-virt-raw-fdt/entry.o: \
	targets/qemu-aarch64-virt-raw-fdt/entry.S
	@mkdir -p $(@D)
	$(AARCH64_CC) --target=aarch64-none-elf -c $< -o $@

$(QEMU_RAW_ELF): $(QEMU_RAW_OBJS) targets/qemu-aarch64-virt-raw-fdt/linker.ld
	$(LD_LLD) -m aarch64elf -T targets/qemu-aarch64-virt-raw-fdt/linker.ld \
		-Map=$(QEMU_RAW_DIR)/ribon.map -o $@ $(QEMU_RAW_OBJS)

$(QEMU_RAW_IMAGE): $(QEMU_RAW_ELF)
	$(OBJCOPY) -O binary $< $@

qemu-aarch64-virt-raw-fdt: $(QEMU_RAW_IMAGE)

qemu-aarch64-virt-raw-fdt-smoke: $(QEMU_RAW_IMAGE)
	$(PYTHON) tools/qemu_target_smoke.py \
		--target aarch64-virt-raw-fdt --qemu $(QEMU_AARCH64) \
		--image $(QEMU_RAW_IMAGE) \
		--payload $(QEMU_RAW_FIXTURE) --expected-payload-class fixture \
		--source-revision $(shell git rev-parse HEAD) \
		--log $(RESULTS_DIR)/qemu-aarch64-virt-raw-fdt.log \
		--result $(RESULTS_DIR)/qemu-aarch64-virt-raw-fdt.json

$(QEMU_LINUX_EXTERNAL_STAMP): $(QEMU_LINUX_INPUT_DESCRIPTOR) \
		$(QEMU_LINUX_MANIFEST) tools/prepare_external_linux_image.py
	$(PYTHON) tools/prepare_external_linux_image.py \
		--descriptor $(QEMU_LINUX_INPUT_DESCRIPTOR) \
		--cache $(QEMU_LINUX_CACHE) \
		--product-manifest $(QEMU_LINUX_MANIFEST) \
		--assembly $(QEMU_LINUX_EXTERNAL_ASM) \
		--result $(QEMU_LINUX_EXTERNAL_VALIDATION) --allow-download
	@touch $@

$(QEMU_LINUX_EXTERNAL_ASM) $(QEMU_LINUX_EXTERNAL_VALIDATION): \
		$(QEMU_LINUX_EXTERNAL_STAMP)
	@test -f $@

$(QEMU_LINUX_INIT_OBJ): tests/fixtures/linux/aarch64/init.S
	@mkdir -p $(@D)
	$(AARCH64_CC) --target=aarch64-none-elf -c $< -o $@

$(QEMU_LINUX_INIT_ELF): $(QEMU_LINUX_INIT_OBJ) \
		tests/fixtures/linux/aarch64/init.ld
	$(LD_LLD) -m aarch64elf -static -T tests/fixtures/linux/aarch64/init.ld \
		-o $@ $(QEMU_LINUX_INIT_OBJ)

$(QEMU_LINUX_INITRAMFS_STAMP): $(QEMU_LINUX_INIT_ELF) \
		tools/build_linux_initramfs.py
	$(PYTHON) tools/build_linux_initramfs.py --init $(QEMU_LINUX_INIT_ELF) \
		--output $(QEMU_LINUX_INITRAMFS) \
		--component-manifest $(QEMU_LINUX_MODULE_MANIFEST)
	@touch $@

$(QEMU_LINUX_INITRAMFS) $(QEMU_LINUX_MODULE_MANIFEST): \
		$(QEMU_LINUX_INITRAMFS_STAMP)
	@test -f $@

qemu-aarch64-virt-linux-product: $(QEMU_LINUX_EXTERNAL_STAMP) \
		$(QEMU_LINUX_INITRAMFS_STAMP)
	$(MAKE) --no-print-directory \
		QEMU_RAW_DIR=$(abspath $(QEMU_LINUX_DIR)) \
		QEMU_RAW_MANIFEST=$(QEMU_LINUX_MANIFEST) \
		QEMU_RAW_PAYLOAD=$(abspath $(QEMU_LINUX_CACHE)) \
		QEMU_RAW_EXTERNAL_PAYLOAD_ASM=$(abspath $(QEMU_LINUX_EXTERNAL_ASM)) \
		QEMU_RAW_MODULE_COMPONENT_MANIFEST=$(abspath $(QEMU_LINUX_MODULE_MANIFEST)) \
		QEMU_RAW_CPPFLAGS=-DRIBON_PORT_PAYLOAD_SIZE=33554432ull \
		RAW_IMAGE_FORMAT_SRCS=src/image-formats/linux_aarch64.c \
		RAW_PROTOCOL_SRCS="src/protocols/os/linux/protocol.c src/protocols/os/linux/fdt.c" \
		$(abspath $(QEMU_LINUX_IMAGE))

qemu-aarch64-virt-linux-smoke: qemu-aarch64-virt-linux-product
	$(PYTHON) tools/qemu_target_smoke.py \
		--target aarch64-virt-raw-fdt --qemu $(QEMU_AARCH64) \
		--image $(QEMU_LINUX_IMAGE) --payload $(QEMU_LINUX_CACHE) \
		--expected-payload-class linux-image \
		--expected-payload-sha256 cc281030454415267654a53c0d85f7bea79846258f1409bacfdf814d40ffede1 \
		--product-manifest $(QEMU_LINUX_MANIFEST) \
		--module-provenance $(QEMU_LINUX_MODULE_PROVENANCE) \
		--external-payload-validation $(QEMU_LINUX_EXTERNAL_VALIDATION) \
		--preload-payload-address 0x41000000 \
		--kernel-command-line "console=ttyAMA0 earlycon=pl011,0x09000000 rdinit=/init panic=-1" \
		--expect-clean-exit \
		--required-marker-anywhere RIBON:LINUX:PID1:v1:OK \
		--required-marker-anywhere "reboot: Power down" \
		--source-revision $(shell git rev-parse HEAD) --timeout 40 \
		--log $(RESULTS_DIR)/qemu-aarch64-virt-linux.log \
		--result $(RESULTS_DIR)/qemu-aarch64-virt-linux.json

qemu-aarch64-virt-parus-product:
	@test -n "$(QEMU_PARUS_PAYLOAD)" || \
		{ echo "QEMU_PARUS_PAYLOAD is required" >&2; exit 2; }
	$(PYTHON) tools/validate_external_parus_payload.py \
		--manifest $(QEMU_PARUS_MANIFEST) \
		--payload $(QEMU_PARUS_PAYLOAD) \
		--result $(QEMU_PARUS_VALIDATION)
	$(MAKE) --no-print-directory \
		QEMU_RAW_DIR=$(QEMU_PARUS_DIR) \
		QEMU_RAW_MANIFEST=$(QEMU_PARUS_MANIFEST) \
		QEMU_RAW_PAYLOAD=$(abspath $(QEMU_PARUS_PAYLOAD)) \
		$(QEMU_PARUS_IMAGE)

qemu-aarch64-virt-parus-smoke: qemu-aarch64-virt-parus-product
	$(PYTHON) tools/qemu_target_smoke.py \
		--target aarch64-virt-raw-fdt --qemu $(QEMU_AARCH64) \
		--image $(QEMU_PARUS_IMAGE) \
		--payload $(QEMU_PARUS_PAYLOAD) --expected-payload-class kernel \
		--product-manifest $(QEMU_PARUS_MANIFEST) \
		$(PARUS_SUCCESS_MARKER_ARGS) \
		--source-revision $(shell git rev-parse HEAD) \
		--log $(RESULTS_DIR)/qemu-aarch64-virt-parus.log \
		--result $(RESULTS_DIR)/qemu-aarch64-virt-parus.json

qemu-aarch64-virt-modules-fixture-product:
	$(MAKE) --no-print-directory \
		QEMU_RAW_DIR=$(abspath $(QEMU_MODULE_FIXTURE_DIR)) \
		QEMU_RAW_MANIFEST=$(QEMU_MODULE_FIXTURE_MANIFEST) \
		QEMU_RAW_MODULE_COMPONENT_MANIFEST=$(abspath $(QEMU_MODULE_FIXTURE_COMPONENT_MANIFEST)) \
		$(abspath $(QEMU_MODULE_FIXTURE_IMAGE))

qemu-aarch64-virt-modules-fixture-smoke: \
	qemu-aarch64-virt-modules-fixture-product
	$(PYTHON) tools/qemu_target_smoke.py \
		--target aarch64-virt-raw-fdt --qemu $(QEMU_AARCH64) \
		--image $(QEMU_MODULE_FIXTURE_IMAGE) \
		--payload $(QEMU_MODULE_FIXTURE_PAYLOAD) \
		--expected-payload-class fixture \
		--product-manifest $(QEMU_MODULE_FIXTURE_MANIFEST) \
		--module-provenance $(QEMU_MODULE_FIXTURE_PROVENANCE) \
		--required-marker-anywhere RIBON-RFDT-MODULES=0x0000000000000008 \
		--required-marker-anywhere RIBON-RFDT-INITIAL-IMAGES=0x0000000000000001 \
		--source-revision $(shell git rev-parse HEAD) \
		--log $(RESULTS_DIR)/qemu-aarch64-virt-modules-fixture.log \
		--result $(RESULTS_DIR)/qemu-aarch64-virt-modules-fixture.json

qemu-module-input-force:

qemu-aarch64-virt-parus-modules-product: qemu-module-input-force
	@test -n "$(QEMU_PARUS_PAYLOAD)" || \
		{ echo "QEMU_PARUS_PAYLOAD is required" >&2; exit 2; }
	@test -n "$(QEMU_PARUS_MODULE_COMPONENT_MANIFEST)" || \
		{ echo "QEMU_PARUS_MODULE_COMPONENT_MANIFEST is required" >&2; exit 2; }
	$(PYTHON) tools/validate_external_parus_payload.py \
		--manifest $(QEMU_PARUS_MODULE_PRODUCT_MANIFEST) \
		--payload $(QEMU_PARUS_PAYLOAD) \
		--result $(QEMU_PARUS_MODULE_VALIDATION)
	$(MAKE) --no-print-directory \
		QEMU_RAW_DIR=$(abspath $(QEMU_PARUS_MODULE_DIR)) \
		QEMU_RAW_MANIFEST=$(QEMU_PARUS_MODULE_PRODUCT_MANIFEST) \
		QEMU_RAW_PAYLOAD=$(abspath $(QEMU_PARUS_PAYLOAD)) \
		QEMU_RAW_MODULE_COMPONENT_MANIFEST=$(abspath $(QEMU_PARUS_MODULE_COMPONENT_MANIFEST)) \
		$(abspath $(QEMU_PARUS_MODULE_IMAGE))

qemu-aarch64-virt-parus-modules-smoke: \
	qemu-aarch64-virt-parus-modules-product
	$(PYTHON) tools/qemu_target_smoke.py \
		--target aarch64-virt-raw-fdt --qemu $(QEMU_AARCH64) \
		--image $(QEMU_PARUS_MODULE_IMAGE) \
		--payload $(QEMU_PARUS_PAYLOAD) --expected-payload-class kernel \
		--product-manifest $(QEMU_PARUS_MODULE_PRODUCT_MANIFEST) \
		--module-provenance $(QEMU_PARUS_MODULE_PROVENANCE) \
		--required-marker-anywhere RIBON-RFDT-MODULES=0x0000000000000001 \
		--required-marker-anywhere RIBON-RFDT-INITIAL-IMAGES=0x0000000000000001 \
		--required-marker-anywhere PARUS:RUNTIME:v0:EXTERNAL_INITIAL_USER:ARTIFACT=ELF64:ROLE=INITIAL_IMAGE \
		--required-marker-anywhere MODULES=1:RESULT=OK \
		$(PARUS_SUCCESS_MARKER_ARGS) \
		--source-revision $(shell git rev-parse HEAD) \
		--log $(RESULTS_DIR)/qemu-aarch64-virt-parus-modules.log \
		--result $(RESULTS_DIR)/qemu-aarch64-virt-parus-modules.json

$(QEMU_RISCV64_REGISTRY_C): $(QEMU_RISCV64_MANIFEST) tools/generate_plugin_registry.py
	$(PYTHON) tools/generate_plugin_registry.py --manifest $< \
		--output $@ --report $(QEMU_RISCV64_GRAPH)

$(QEMU_RISCV64_EMBED_C): $(QEMU_RISCV64_PAYLOAD) tools/embed_binary.py
	$(PYTHON) tools/embed_binary.py --input $< --output $@

$(QEMU_RISCV64_DIR)/obj/%.o: %.c
	@mkdir -p $(@D)
	$(RISCV64_CC) $(RISCV64_FLAGS) $(QEMU_RISCV64_CPPFLAGS) \
		$(QEMU_RISCV64_MODULE_CPPFLAGS) $(DEPFLAGS) -c $< -o $@

ifneq ($(strip $(QEMU_RISCV64_MODULE_COMPONENT_MANIFEST)),)
$(QEMU_RISCV64_MODULE_STAMP): \
	$(QEMU_RISCV64_MODULE_COMPONENT_MANIFEST) \
	$(QEMU_RISCV64_MANIFEST) \
	tools/generate_boot_module_bundle.py boot-module-input-force
	$(PYTHON) tools/generate_boot_module_bundle.py \
		--manifest $(QEMU_RISCV64_MODULE_COMPONENT_MANIFEST) \
		--product-manifest $(QEMU_RISCV64_MANIFEST) \
		--output-root $(QEMU_RISCV64_DIR) \
		--assembly $(QEMU_RISCV64_MODULE_ASM) \
		--descriptors $(QEMU_RISCV64_MODULE_C) \
		--provenance $(QEMU_RISCV64_MODULE_PROVENANCE)
	@touch $@

$(QEMU_RISCV64_MODULE_ASM) $(QEMU_RISCV64_MODULE_C) \
		$(QEMU_RISCV64_MODULE_PROVENANCE): $(QEMU_RISCV64_MODULE_STAMP)
	@test -f $@

$(QEMU_RISCV64_DIR)/obj/generated/boot-modules/descriptor.o: \
	$(QEMU_RISCV64_MODULE_C)
	@mkdir -p $(@D)
	$(RISCV64_CC) $(RISCV64_FLAGS) $(DEPFLAGS) -c $< -o $@

$(QEMU_RISCV64_DIR)/obj/generated/boot-modules/bundle.o: \
	$(QEMU_RISCV64_MODULE_ASM)
	@mkdir -p $(@D)
	$(RISCV64_CC) --target=riscv64-none-elf -march=rv64gc \
		-mabi=lp64d -mcmodel=medany -I$(QEMU_RISCV64_MODULE_DIR) \
		-c $< -o $@
endif

$(QEMU_RISCV64_DIR)/obj/generated/plugin_registry.o: $(QEMU_RISCV64_REGISTRY_C)
	@mkdir -p $(@D)
	$(RISCV64_CC) $(RISCV64_FLAGS) $(DEPFLAGS) -c $< -o $@

$(QEMU_RISCV64_DIR)/obj/generated/embedded_payload.o: $(QEMU_RISCV64_EMBED_C)
	@mkdir -p $(@D)
	$(RISCV64_CC) $(RISCV64_FLAGS) $(DEPFLAGS) -c $< -o $@

ifneq ($(strip $(QEMU_RISCV64_EXTERNAL_PAYLOAD_ASM)),)
$(QEMU_RISCV64_DIR)/obj/generated/external_payload.o: \
	$(QEMU_RISCV64_EXTERNAL_PAYLOAD_ASM)
	@mkdir -p $(@D)
	$(RISCV64_CC) --target=riscv64-none-elf -march=rv64gc \
		-mabi=lp64d -mcmodel=medany -c $< -o $@
endif

$(QEMU_RISCV64_DIR)/obj/targets/qemu-riscv64-virt-opensbi/entry.o: \
	targets/qemu-riscv64-virt-opensbi/entry.S
	@mkdir -p $(@D)
	$(RISCV64_CC) --target=riscv64-none-elf -march=rv64gc \
		-mabi=lp64d -mcmodel=medany -c $< -o $@

$(QEMU_RISCV64_RLH1_FIXTURE_DIR)/obj/tests/fixtures/riscv64/rlh1_consumer.o: \
	tests/fixtures/riscv64/rlh1_consumer.c
	@mkdir -p $(@D)
	$(RISCV64_CC) $(RISCV64_FLAGS) $(DEPFLAGS) -c $< -o $@

$(QEMU_RISCV64_RLH1_FIXTURE_DIR)/obj/tests/fixtures/riscv64/rlh1_consumer_entry.o: \
	tests/fixtures/riscv64/rlh1_consumer_entry.S
	@mkdir -p $(@D)
	$(RISCV64_CC) --target=riscv64-none-elf -march=rv64gc \
		-mabi=lp64d -mcmodel=medany -c $< -o $@

$(QEMU_RISCV64_RLH1_FIXTURE_PAYLOAD): \
	$(QEMU_RISCV64_RLH1_FIXTURE_OBJS) \
	tests/fixtures/riscv64/rlh1_consumer.ld
	$(LD_LLD) -m elf64lriscv \
		-T tests/fixtures/riscv64/rlh1_consumer.ld \
		-Map=$(QEMU_RISCV64_RLH1_FIXTURE_DIR)/payload.map \
		-o $@ $(QEMU_RISCV64_RLH1_FIXTURE_OBJS)

$(QEMU_RISCV64_ELF): $(QEMU_RISCV64_OBJS) \
	targets/qemu-riscv64-virt-opensbi/linker.ld
	$(LD_LLD) -m elf64lriscv \
		-T targets/qemu-riscv64-virt-opensbi/linker.ld \
		-Map=$(QEMU_RISCV64_DIR)/ribon.map \
		-o $@ $(QEMU_RISCV64_OBJS)

$(QEMU_RISCV64_IMAGE): $(QEMU_RISCV64_ELF)
	$(OBJCOPY) -O binary $< $@

qemu-riscv64-virt-parus-product:
	@test -n "$(QEMU_RISCV64_PAYLOAD)" || \
		{ echo "QEMU_RISCV64_PAYLOAD is required" >&2; exit 2; }
	$(PYTHON) tools/validate_external_parus_payload.py \
		--manifest $(QEMU_RISCV64_MANIFEST) \
		--payload $(QEMU_RISCV64_PAYLOAD) \
		--result $(QEMU_RISCV64_VALIDATION)
	$(MAKE) --no-print-directory \
		QEMU_RISCV64_PAYLOAD=$(abspath $(QEMU_RISCV64_PAYLOAD)) \
		$(QEMU_RISCV64_IMAGE)

qemu-riscv64-virt-parus-smoke: qemu-riscv64-virt-parus-product
	$(PYTHON) tools/qemu_target_smoke.py \
		--target riscv64-virt-opensbi --qemu $(QEMU_RISCV64) \
		--image $(QEMU_RISCV64_IMAGE) \
		--firmware $(RISCV64_OPENSBI_FIRMWARE) \
		--payload $(QEMU_RISCV64_PAYLOAD) --expected-payload-class kernel \
		--product-manifest $(QEMU_RISCV64_MANIFEST) \
		$(PARUS_SUCCESS_MARKER_ARGS) \
		--source-revision $(shell git rev-parse HEAD) \
		--log $(RESULTS_DIR)/qemu-riscv64-virt-parus.log \
		--result $(RESULTS_DIR)/qemu-riscv64-virt-parus.json

qemu-riscv64-virt-rlh1-fixture-product: \
	$(QEMU_RISCV64_RLH1_FIXTURE_PAYLOAD)
	$(MAKE) --no-print-directory \
		QEMU_RISCV64_DIR=$(abspath $(QEMU_RISCV64_RLH1_FIXTURE_DIR)) \
		QEMU_RISCV64_MANIFEST=$(QEMU_RISCV64_RLH1_FIXTURE_MANIFEST) \
		QEMU_RISCV64_PAYLOAD=$(abspath $(QEMU_RISCV64_RLH1_FIXTURE_PAYLOAD)) \
		$(abspath $(QEMU_RISCV64_RLH1_FIXTURE_IMAGE))

qemu-riscv64-virt-rlh1-fixture-smoke: \
	qemu-riscv64-virt-rlh1-fixture-product
	$(PYTHON) tools/qemu_target_smoke.py \
		--target riscv64-virt-opensbi --qemu $(QEMU_RISCV64) \
		--image $(QEMU_RISCV64_RLH1_FIXTURE_IMAGE) \
		--firmware $(RISCV64_OPENSBI_FIRMWARE) \
		--payload $(QEMU_RISCV64_RLH1_FIXTURE_PAYLOAD) \
		--expected-payload-class fixture \
		--product-manifest $(QEMU_RISCV64_RLH1_FIXTURE_MANIFEST) \
		--required-marker RIBON-RLH1-RISCV64-FIXTURE-ENTRY \
		--required-marker RIBON-RLH1-RISCV64-FIXTURE-MMU-OFF \
		--required-marker RIBON-RLH1-RISCV64-FIXTURE-RLH1-OK \
		--required-marker RIBON-RLH1-RISCV64-FIXTURE-BOOT-CPU-OK \
		--source-revision $(shell git rev-parse HEAD) \
		--log $(RESULTS_DIR)/qemu-riscv64-virt-rlh1-fixture.log \
		--result $(RESULTS_DIR)/qemu-riscv64-virt-rlh1-fixture.json

$(QEMU_RISCV64_LINUX_EXTERNAL_STAMP): \
		$(QEMU_RISCV64_LINUX_INPUT_DESCRIPTOR) \
		$(QEMU_RISCV64_LINUX_MANIFEST) \
		tools/prepare_external_linux_riscv64_image.py
	$(PYTHON) tools/prepare_external_linux_riscv64_image.py \
		--descriptor $(QEMU_RISCV64_LINUX_INPUT_DESCRIPTOR) \
		--cache $(QEMU_RISCV64_LINUX_CACHE) \
		--product-manifest $(QEMU_RISCV64_LINUX_MANIFEST) \
		--assembly $(QEMU_RISCV64_LINUX_EXTERNAL_ASM) \
		--result $(QEMU_RISCV64_LINUX_EXTERNAL_VALIDATION) \
		--allow-download
	@touch $@

$(QEMU_RISCV64_LINUX_EXTERNAL_ASM) \
		$(QEMU_RISCV64_LINUX_EXTERNAL_VALIDATION): \
		$(QEMU_RISCV64_LINUX_EXTERNAL_STAMP)
	@test -f $@

$(QEMU_RISCV64_LINUX_INIT_OBJ): tests/fixtures/linux/riscv64/init.S
	@mkdir -p $(@D)
	$(RISCV64_CC) --target=riscv64-none-elf -march=rv64gc \
		-mabi=lp64d -mcmodel=medany -c $< -o $@

$(QEMU_RISCV64_LINUX_INIT_ELF): $(QEMU_RISCV64_LINUX_INIT_OBJ) \
		tests/fixtures/linux/riscv64/init.ld
	$(LD_LLD) -m elf64lriscv -static \
		-T tests/fixtures/linux/riscv64/init.ld \
		-o $@ $(QEMU_RISCV64_LINUX_INIT_OBJ)

$(QEMU_RISCV64_LINUX_INITRAMFS_STAMP): \
		$(QEMU_RISCV64_LINUX_INIT_ELF) tools/build_linux_initramfs.py
	$(PYTHON) tools/build_linux_initramfs.py --architecture riscv64 \
		--init $(QEMU_RISCV64_LINUX_INIT_ELF) \
		--output $(QEMU_RISCV64_LINUX_INITRAMFS) \
		--component-manifest $(QEMU_RISCV64_LINUX_MODULE_MANIFEST)
	@touch $@

$(QEMU_RISCV64_LINUX_INITRAMFS) \
		$(QEMU_RISCV64_LINUX_MODULE_MANIFEST): \
		$(QEMU_RISCV64_LINUX_INITRAMFS_STAMP)
	@test -f $@

qemu-riscv64-virt-linux-product: \
		$(QEMU_RISCV64_LINUX_EXTERNAL_STAMP) \
		$(QEMU_RISCV64_LINUX_INITRAMFS_STAMP)
	$(MAKE) --no-print-directory \
		QEMU_RISCV64_DIR=$(abspath $(QEMU_RISCV64_LINUX_DIR)) \
		QEMU_RISCV64_MANIFEST=$(QEMU_RISCV64_LINUX_MANIFEST) \
		QEMU_RISCV64_PAYLOAD=$(abspath $(QEMU_RISCV64_LINUX_CACHE)) \
		QEMU_RISCV64_EXTERNAL_PAYLOAD_ASM=$(abspath $(QEMU_RISCV64_LINUX_EXTERNAL_ASM)) \
		QEMU_RISCV64_MODULE_COMPONENT_MANIFEST=$(abspath $(QEMU_RISCV64_LINUX_MODULE_MANIFEST)) \
		RAW_IMAGE_FORMAT_SRCS=src/image-formats/linux_riscv64.c \
		RAW_PROTOCOL_SRCS="src/protocols/os/linux/protocol.c src/protocols/os/linux/fdt.c" \
		$(abspath $(QEMU_RISCV64_LINUX_IMAGE))

qemu-riscv64-virt-linux-smoke: qemu-riscv64-virt-linux-product
	$(PYTHON) tools/qemu_target_smoke.py \
		--target riscv64-virt-opensbi --qemu $(QEMU_RISCV64) \
		--image $(QEMU_RISCV64_LINUX_IMAGE) \
		--firmware $(RISCV64_OPENSBI_FIRMWARE) \
		--payload $(QEMU_RISCV64_LINUX_CACHE) \
		--expected-payload-class linux-riscv64-image \
		--expected-payload-sha256 c601b3ef8415fb0309c5098569cab61954916a9388fba929a32e11f024e8490a \
		--product-manifest $(QEMU_RISCV64_LINUX_MANIFEST) \
		--module-provenance $(QEMU_RISCV64_LINUX_MODULE_PROVENANCE) \
		--external-payload-validation $(QEMU_RISCV64_LINUX_EXTERNAL_VALIDATION) \
		--preload-payload-address 0x80400000 \
		--kernel-command-line "console=ttyS0 earlycon=sbi rdinit=/init panic=-1" \
		--expect-clean-exit \
		--required-marker-anywhere RIBON-RFDT-FIRMWARE-REGIONS=0x0000000000000003 \
		--required-marker-anywhere RIBON:LINUX:PID1:v1:OK \
		--required-marker-anywhere "reboot: Power down" \
		--source-revision $(shell git rev-parse HEAD) --timeout 60 \
		--log $(RESULTS_DIR)/qemu-riscv64-virt-linux.log \
		--result $(RESULTS_DIR)/qemu-riscv64-virt-linux.json

check-linux-riscv64: check-linux-boot check-linux-external-input \
	qemu-riscv64-virt-linux-smoke

check-multi-os-runtime: \
	qemu-aarch64-virt-linux-smoke \
	x86_64-uefi-linux-smoke \
	x86_64-uefi-freebsd-smoke \
	qemu-riscv64-virt-linux-smoke \
	qemu-aarch64-virt-raw-fdt-smoke \
	x86_64-uefi-parus-fixture-smoke \
	qemu-riscv64-virt-rlh1-fixture-smoke
	$(PYTHON) tools/check_multi_os_runtime.py \
		--source-revision $(shell git rev-parse HEAD) \
		--linux-aarch64-raw-fdt $(RESULTS_DIR)/qemu-aarch64-virt-linux.json \
		--linux-x86_64-uefi $(UEFI_LINUX_DIR)/results/qemu.json \
		--freebsd-amd64-uefi $(UEFI_FREEBSD_DIR)/results/qemu.json \
		--linux-riscv64-opensbi $(RESULTS_DIR)/qemu-riscv64-virt-linux.json \
		--parus-aarch64-rlh1-fixture $(RESULTS_DIR)/qemu-aarch64-virt-raw-fdt.json \
		--parus-x86_64-rlh1-fixture $(UEFI_FIXTURE_DIR)/results/qemu.json \
		--parus-riscv64-rlh1-fixture $(RESULTS_DIR)/qemu-riscv64-virt-rlh1-fixture.json \
		--output $(RESULTS_DIR)/multi-os/R04/matrix.json

$(RPI5_PARUS_VALIDATION): \
	$(RPI5_EXTERNAL_MANIFEST) $(RPI5_SELECTED_PAYLOAD) \
	tools/validate_external_parus_payload.py rpi5-external-input-force
	$(PYTHON) tools/validate_external_parus_payload.py \
		--manifest $(RPI5_EXTERNAL_MANIFEST) \
		--payload $(RPI5_SELECTED_PAYLOAD) \
		--result $@

rpi5-external-input-force:

$(RPI5_REGISTRY_C): $(RPI5_SELECTED_MANIFEST) tools/generate_plugin_registry.py
	$(PYTHON) tools/generate_plugin_registry.py --manifest $< \
		--output $@ --report $(RPI5_GRAPH)

$(RPI5_FIXTURE): tools/make_elf64_fixture.py
	@mkdir -p $(@D)
	$(PYTHON) $< --arch aarch64 --base 0x4000000 --entry-at-base --output $@

$(RPI5_EMBED_C): \
	$(RPI5_SELECTED_PAYLOAD) $(RPI5_SELECTED_VALIDATION) tools/embed_binary.py
	$(PYTHON) tools/embed_binary.py --input $< --output $@

rpi5-module-input-force:

ifneq ($(strip $(RPI5_MODULE_COMPONENT_MANIFEST)),)
$(RPI5_MODULE_STAMP): \
	$(RPI5_MODULE_COMPONENT_MANIFEST) \
	$(RPI5_SELECTED_MANIFEST) \
	tools/generate_boot_module_bundle.py rpi5-module-input-force
	$(PYTHON) tools/generate_boot_module_bundle.py \
		--manifest $(RPI5_MODULE_COMPONENT_MANIFEST) \
		--product-manifest $(RPI5_SELECTED_MANIFEST) \
		--output-root $(RPI5_DIR) \
		--assembly $(RPI5_MODULE_ASM) \
		--descriptors $(RPI5_MODULE_C) \
		--provenance $(RPI5_MODULE_PROVENANCE)
	@touch $@

$(RPI5_MODULE_ASM) $(RPI5_MODULE_C) $(RPI5_MODULE_PROVENANCE): \
	$(RPI5_MODULE_STAMP)
	@test -f $@

$(RPI5_DIR)/obj/generated/boot-modules/descriptor.o: $(RPI5_MODULE_C)
	@mkdir -p $(@D)
	$(AARCH64_CC) $(AARCH64_FLAGS) $(DEPFLAGS) -c $< -o $@

$(RPI5_DIR)/obj/generated/boot-modules/bundle.o: $(RPI5_MODULE_ASM)
	@mkdir -p $(@D)
	$(AARCH64_CC) --target=aarch64-none-elf \
		-I$(RPI5_MODULE_DIR) -c $< -o $@
endif

$(RPI5_DIR)/obj/%.o: %.c
	@mkdir -p $(@D)
	$(AARCH64_CC) $(AARCH64_FLAGS) $(RPI5_MODULE_CPPFLAGS) \
		$(DEPFLAGS) -c $< -o $@

$(RPI5_DIR)/obj/generated/plugin_registry.o: $(RPI5_REGISTRY_C)
	@mkdir -p $(@D)
	$(AARCH64_CC) $(AARCH64_FLAGS) $(DEPFLAGS) -c $< -o $@

$(RPI5_DIR)/obj/generated/embedded_payload.o: $(RPI5_EMBED_C)
	@mkdir -p $(@D)
	$(AARCH64_CC) $(AARCH64_FLAGS) $(DEPFLAGS) -c $< -o $@

$(RPI5_DIR)/obj/targets/rpi5-aarch64-raw-fdt/entry.o: \
	targets/rpi5-aarch64-raw-fdt/entry.S
	@mkdir -p $(@D)
	$(AARCH64_CC) --target=aarch64-none-elf -c $< -o $@

$(RPI5_ELF): $(RPI5_OBJS) targets/rpi5-aarch64-raw-fdt/linker.ld
	$(LD_LLD) -m aarch64elf -T targets/rpi5-aarch64-raw-fdt/linker.ld \
		-Map=$(RPI5_DIR)/ribon.map -o $@ $(RPI5_OBJS)

$(RPI5_IMAGE): $(RPI5_ELF)
	$(OBJCOPY) -O binary $< $@

rpi5-aarch64-raw-fdt-package: $(RPI5_IMAGE) $(RPI5_SELECTED_PAYLOAD) \
	$(RPI5_MODULE_PROVENANCE)
	$(PYTHON) tools/package_rpi5.py \
		--image $(RPI5_IMAGE) --payload $(RPI5_SELECTED_PAYLOAD) \
		--config targets/rpi5-aarch64-raw-fdt/package/config.txt \
		--cmdline targets/rpi5-aarch64-raw-fdt/package/cmdline.txt \
		$(if $(strip $(RPI5_MODULE_PROVENANCE)),--module-provenance $(RPI5_MODULE_PROVENANCE)) \
		$(if $(strip $(RPI5_MODULE_PROVENANCE)),--product-manifest $(RPI5_SELECTED_MANIFEST)) \
		--output $(RPI5_PACKAGE)
	$(PYTHON) tools/check_rpi_package.py $(RPI5_PACKAGE)

rpi5-aarch64-parus-package:
	@test -n "$(RPI5_PARUS_PAYLOAD)" || \
		{ echo "RPI5_PARUS_PAYLOAD is required" >&2; exit 2; }
	$(MAKE) --no-print-directory \
		RPI5_PARUS_PAYLOAD=$(abspath $(RPI5_PARUS_PAYLOAD)) \
		rpi5-aarch64-raw-fdt-package

rpi5-aarch64-modules-fixture-package:
	$(MAKE) --no-print-directory \
		RPI5_DIR=$(abspath $(RPI5_MODULE_FIXTURE_DIR)) \
		RPI5_MANIFEST=$(RPI5_MODULE_FIXTURE_MANIFEST) \
		RPI5_MODULE_COMPONENT_MANIFEST=$(abspath $(RPI5_MODULE_FIXTURE_COMPONENT_MANIFEST)) \
		rpi5-aarch64-raw-fdt-package

rpi5-aarch64-parus-modules-package:
	@test -n "$(RPI5_PARUS_PAYLOAD)" || \
		{ echo "RPI5_PARUS_PAYLOAD is required" >&2; exit 2; }
	@test -n "$(RPI5_PARUS_MODULE_COMPONENT_MANIFEST)" || \
		{ echo "RPI5_PARUS_MODULE_COMPONENT_MANIFEST is required" >&2; exit 2; }
	$(MAKE) --no-print-directory \
		RPI5_DIR=$(abspath $(RPI5_PARUS_MODULE_DIR)) \
		RPI5_EXTERNAL_MANIFEST=$(RPI5_PARUS_MODULE_MANIFEST) \
		RPI5_PARUS_PAYLOAD=$(abspath $(RPI5_PARUS_PAYLOAD)) \
		RPI5_MODULE_COMPONENT_MANIFEST=$(abspath $(RPI5_PARUS_MODULE_COMPONENT_MANIFEST)) \
		rpi5-aarch64-raw-fdt-package

$(UEFI_FIXTURE_INPUT_MANIFEST): $(UEFI_FIXTURE_MANIFEST)
	@mkdir -p $(@D)
	cp $< $@

$(UEFI_EXTERNAL_INPUT_MANIFEST): $(UEFI_EXTERNAL_MANIFEST)
	@mkdir -p $(@D)
	cp $< $@

$(UEFI_EXTERNAL_VALIDATION): \
	$(UEFI_EXTERNAL_MANIFEST) $(UEFI_EXTERNAL_SOURCE) \
	tools/validate_external_parus_payload.py uefi-external-input-force
	@test -n "$(UEFI_EXTERNAL_SOURCE)" || \
		{ echo "UEFI_PARUS_PAYLOAD is required" >&2; exit 2; }
	$(PYTHON) tools/validate_external_parus_payload.py \
		--manifest $(UEFI_EXTERNAL_MANIFEST) \
		--payload $(UEFI_EXTERNAL_SOURCE) \
		--result $@
