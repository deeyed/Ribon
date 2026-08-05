CORE_LIB := $(BUILD_DIR)/libribon-core.a
BOOT_LIB := $(BUILD_DIR)/libribon-boot.a
SDK_LIB := $(BUILD_DIR)/libribon-sdk.a
SDK_UPDATE_LIB := $(BUILD_DIR)/libribon-update.a
RIBOS_POLICY_LIB := $(BUILD_DIR)/libribon-policy-ribos.a
RIBOS_POLICY_OBJ := $(BUILD_DIR)/obj/src/plugins/policy/ribos/adapter.o
HOST_REFERENCE := $(BUILD_DIR)/ribon-host-reference
KERNEL_FIXTURE := $(BUILD_DIR)/fixtures/kernel.elf
HOST_MANIFEST := qstar/manifests/host-reference.json
GENERATED_REGISTRY_C := $(BUILD_DIR)/generated/plugin_registry.c
GENERATED_REGISTRY_REPORT := $(BUILD_DIR)/results/host-reference-object-graph.json
GENERATED_REGISTRY_O := $(BUILD_DIR)/obj/generated/plugin_registry.o
TEST_BUILD_DIR := $(BUILD_ROOT)/tests
TARGET_BUILD_ROOT := $(BUILD_ROOT)/targets
RESULTS_DIR := $(BUILD_ROOT)/results
RIBOS_PARSER_PILOT := $(BUILD_ROOT)/tools/ribosc
RIBOS_SCHEMA_TEST := $(TEST_BUILD_DIR)/ribos_schema_tests
RIBOS_IR_MODULE_TEST := $(TEST_BUILD_DIR)/ribos_ir_module_tests
RIBOS_IR_RESOURCE_TEST := $(TEST_BUILD_DIR)/ribos_ir_resource_tests
RIBOS_ARTIFACT_TEST := $(TEST_BUILD_DIR)/ribos_artifact_tests
RIBOS_ALLOCATOR_TEST := $(TEST_BUILD_DIR)/ribos_allocator_boundary_tests
RIBOS_RUNTIME_CONTRACT_TEST := $(TEST_BUILD_DIR)/ribos_runtime_contract_tests
RIBOS_PREPARED_PROGRAM_TEST := $(TEST_BUILD_DIR)/ribos_prepared_program_tests
RIBOS_RUNTIME_STORAGE_TEST := $(TEST_BUILD_DIR)/ribos_runtime_storage_tests
RIBOS_VM_SCALAR_TEST := $(TEST_BUILD_DIR)/ribos_vm_scalar_tests
RIBOS_VM_CALLS_LOOPS_TEST := $(TEST_BUILD_DIR)/ribos_vm_calls_loops_tests
RIBOS_VM_HANDLES_TEST := $(TEST_BUILD_DIR)/ribos_vm_handles_tests
RIBOS_VM_TERMINAL_TEST := $(TEST_BUILD_DIR)/ribos_vm_terminal_tests
RIBOS_RIBON_INTEGRATION_TEST := $(TEST_BUILD_DIR)/ribos_ribon_integration_tests
RIBOS_HOST_UNSIGNED := $(BUILD_ROOT)/ribos/fixtures/host-policy-unsigned.rba
RIBOS_HOST_SIGNED_A := $(BUILD_ROOT)/ribos/fixtures/host-policy-a.rba
RIBOS_HOST_SIGNED_B := $(BUILD_ROOT)/ribos/fixtures/host-policy-b.rba
RIBOS_HOST_WRONG_KEY := $(BUILD_ROOT)/ribos/fixtures/host-policy-wrong-key.rba
RIBOS_HOST_VERIFIER_INVALID_UNSIGNED := \
	$(BUILD_ROOT)/ribos/fixtures/host-policy-verifier-invalid-unsigned.rba
RIBOS_HOST_VERIFIER_INVALID := \
	$(BUILD_ROOT)/ribos/fixtures/host-policy-verifier-invalid.rba
RIBOS_VERIFIER := $(BUILD_ROOT)/tools/ribos-verify
RIBOS_RUNNER := $(BUILD_ROOT)/tools/ribos-run
SECURITY_BUILD_DIR := $(BUILD_ROOT)/security
UPDATE_MANIFEST_BUILD_DIR := $(BUILD_ROOT)/update-manifest
UPDATE_STORAGE_BUILD_DIR := $(BUILD_ROOT)/update-storage
SECURITY_TEST := $(TEST_BUILD_DIR)/ed25519_provider_tests
SECURITY_SANITIZER_TEST := $(TEST_BUILD_DIR)/ed25519_provider_sanitizer_tests
KEY_POLICY_TEST := $(TEST_BUILD_DIR)/key_policy_tests
KEY_POLICY_SANITIZER_TEST := $(TEST_BUILD_DIR)/key_policy_sanitizer_tests
PROTECTED_STATE_TEST := $(TEST_BUILD_DIR)/protected_state_tests
PROTECTED_STATE_SANITIZER_TEST := \
	$(TEST_BUILD_DIR)/protected_state_sanitizer_tests
UPDATE_MANIFEST_TEST := $(TEST_BUILD_DIR)/update_manifest_tests
UPDATE_MANIFEST_SANITIZER_TEST := \
	$(TEST_BUILD_DIR)/update_manifest_sanitizer_tests
UPDATE_STORAGE_TEST := $(TEST_BUILD_DIR)/update_storage_tests
UPDATE_STORAGE_SANITIZER_TEST := \
	$(TEST_BUILD_DIR)/update_storage_sanitizer_tests
UPDATE_INSTALLER_TEST := $(TEST_BUILD_DIR)/update_installer_tests
UPDATE_POWER_CUT_TEST := $(TEST_BUILD_DIR)/update_power_cut_tests
UPDATE_POWER_CUT_SANITIZER_TEST := \
	$(TEST_BUILD_DIR)/update_power_cut_sanitizer_tests
BOOT_CONFIRMATION_TEST := $(TEST_BUILD_DIR)/boot_confirmation_tests
BOOT_CONFIRMATION_SANITIZER_TEST := \
	$(TEST_BUILD_DIR)/boot_confirmation_sanitizer_tests
UPDATE_POWER_CUT_DIR := $(BUILD_ROOT)/update-power-cut
UPDATE_POWER_CUT_CASES := $(UPDATE_POWER_CUT_DIR)/cases
UPDATE_POWER_CUT_COVERAGE := $(UPDATE_POWER_CUT_DIR)/coverage.json
UEFI_UPDATE_STORAGE_TEST := $(TEST_BUILD_DIR)/uefi_update_storage_tests
RECOVERY_NETWORK_TEST := $(TEST_BUILD_DIR)/recovery_network_tests
RECOVERY_NETWORK_SANITIZER_TEST := \
	$(TEST_BUILD_DIR)/recovery_network_sanitizer_tests
SECURITY_INCLUDE_FLAGS := -Ithird_party/monocypher/4.0.3
SECURITY_KEY_POLICY_SRCS := src/security/key_policy.c
SECURITY_PROTECTED_STATE_SRCS := \
	src/security/sha256.c \
	src/security/protected_state.c
SECURITY_PROVIDER_SRCS := \
	src/security/signature.c \
	src/plugins/security/ed25519/provider.c \
	third_party/monocypher/4.0.3/monocypher.c \
	third_party/monocypher/4.0.3/monocypher-ed25519.c
UPDATE_MANIFEST_SRCS := \
	src/update/manifest.c \
	src/security/sha256.c
UPDATE_STORAGE_SRCS := \
	src/update/storage.c \
	src/update/manifest.c \
	src/security/sha256.c
HOST_SECURITY_SRCS := $(SECURITY_KEY_POLICY_SRCS) \
	$(SECURITY_PROTECTED_STATE_SRCS) $(SECURITY_PROVIDER_SRCS)
HOST_SECURITY_OBJS := $(HOST_SECURITY_SRCS:%.c=$(BUILD_DIR)/obj/%.o)
SDK_UPDATE_SRCS := \
	src/update/manifest.c \
	src/update/storage.c \
	src/update/installer.c \
	src/update/transaction.c \
	src/update/confirmation.c \
	src/security/sha256.c \
	src/security/key_policy.c \
	src/security/protected_state.c \
	src/security/signature.c
SDK_UPDATE_OBJS := $(SDK_UPDATE_SRCS:%.c=$(BUILD_DIR)/obj/%.o)
RIBOS_PEGEN_ROOT ?=
RIBOS_BUILD_DIR := $(BUILD_ROOT)/ribos
RIBOS_OBJECT_DIR := $(RIBOS_BUILD_DIR)/obj
RIBOS_TARGET_CORE_LIB := $(RIBOS_BUILD_DIR)/libribos-target-core.a
RIBOS_HOST_SUPPORT_LIB := $(RIBOS_BUILD_DIR)/libribos-host-support.a
RIBOS_HOST_COMPILER_LIB := $(RIBOS_BUILD_DIR)/libribos-host-compiler.a
RIBOS_TARGET_INCLUDE_FLAGS := \
	-Ilanguage/ribos/base/include \
	-Ilanguage/ribos/schema/include \
	-Ilanguage/ribos/artifact/include \
	-Ilanguage/ribos/artifact/src \
	-Ilanguage/ribos/vm/include
RIBOS_INCLUDE_FLAGS := \
	$(RIBOS_TARGET_INCLUDE_FLAGS) \
	-Ilanguage/ribos/host/include \
	-Ilanguage/ribos/frontend/include \
	-Ilanguage/ribos/frontend/src \
	-Ilanguage/ribos/frontend/generated \
	-Ilanguage/ribos/ir/include \
	-Ilanguage/ribos/ir/src
RIBOS_TARGET_CORE_OBJS := \
	$(RIBOS_OBJECT_DIR)/target/base_allocator.o \
	$(RIBOS_OBJECT_DIR)/target/base_checked.o \
	$(RIBOS_OBJECT_DIR)/target/base_writer.o \
	$(RIBOS_OBJECT_DIR)/target/schema.o \
	$(RIBOS_OBJECT_DIR)/target/artifact_wire.o \
	$(RIBOS_OBJECT_DIR)/target/artifact_sha256.o \
	$(RIBOS_OBJECT_DIR)/target/artifact_codec.o \
	$(RIBOS_OBJECT_DIR)/target/verifier.o \
	$(RIBOS_OBJECT_DIR)/target/prepared.o \
	$(RIBOS_OBJECT_DIR)/target/runtime_storage.o \
	$(RIBOS_OBJECT_DIR)/target/runtime_handles.o \
	$(RIBOS_OBJECT_DIR)/target/runtime_helpers.o \
	$(RIBOS_OBJECT_DIR)/target/runtime_interpreter.o \
	$(RIBOS_OBJECT_DIR)/target/runtime_terminal.o
RIBOS_TARGET_CORE_SRCS := \
	language/ribos/base/src/allocator.c \
	language/ribos/base/src/checked.c \
	language/ribos/base/src/writer.c \
	language/ribos/schema/src/schema.c \
	language/ribos/artifact/src/wire.c \
	language/ribos/artifact/src/sha256.c \
	language/ribos/artifact/src/codec.c \
	language/ribos/vm/src/verifier.c \
	language/ribos/vm/src/prepared.c \
	language/ribos/vm/src/runtime/storage.c \
	language/ribos/vm/src/runtime/handles.c \
	language/ribos/vm/src/runtime/helpers.c \
	language/ribos/vm/src/runtime/interpreter.c \
	language/ribos/vm/src/runtime/terminal.c
RIBOS_HOST_SUPPORT_OBJS := \
	$(RIBOS_OBJECT_DIR)/host-support/allocator.o \
	$(RIBOS_OBJECT_DIR)/host-support/format.o \
	$(RIBOS_OBJECT_DIR)/host-support/writer.o
RIBOS_HOST_COMPILER_OBJS := \
	$(RIBOS_OBJECT_DIR)/host-compiler/ir_module.o \
	$(RIBOS_OBJECT_DIR)/host-compiler/ir_dump.o \
	$(RIBOS_OBJECT_DIR)/host-compiler/ir_analysis.o \
	$(RIBOS_OBJECT_DIR)/host-compiler/artifact_emitter.o \
	$(RIBOS_OBJECT_DIR)/host-compiler/lexer.o \
	$(RIBOS_OBJECT_DIR)/host-compiler/runtime.o \
	$(RIBOS_OBJECT_DIR)/host-compiler/ast.o \
	$(RIBOS_OBJECT_DIR)/host-compiler/parser.o \
	$(RIBOS_OBJECT_DIR)/host-compiler/compiler.o \
	$(RIBOS_OBJECT_DIR)/host-compiler/semantic.o \
	$(RIBOS_OBJECT_DIR)/host-compiler/lower.o \
	$(RIBOS_OBJECT_DIR)/host-compiler/frontend_dump.o \
	$(RIBOS_OBJECT_DIR)/host-compiler/generated_parser.o
RIBOS_HEADERS := \
	language/ribos/base/include/ribos/base/allocator.h \
	language/ribos/base/include/ribos/base/checked.h \
	language/ribos/base/include/ribos/base/writer.h \
	language/ribos/host/include/ribos/host/allocator.h \
	language/ribos/host/include/ribos/host/format.h \
	language/ribos/host/include/ribos/host/writer.h \
	language/ribos/host/include/ribos/host/artifact_emitter.h \
	language/ribos/frontend/include/ribos/frontend/compiler.h \
	language/ribos/frontend/include/ribos/frontend/parser.h \
	language/ribos/schema/include/ribos/schema/schema.h \
	language/ribos/ir/include/ribos/ir/ir.h \
	language/ribos/ir/include/ribos/ir/builder.h \
	language/ribos/ir/include/ribos/ir/analysis.h \
	language/ribos/ir/src/ir_internal.h \
	language/ribos/artifact/include/ribos/artifact/format.h \
	language/ribos/artifact/src/internal.h \
	language/ribos/vm/include/ribos/vm/runtime.h \
	language/ribos/vm/include/ribos/vm/prepared.h \
	language/ribos/vm/include/ribos/vm/storage.h \
	language/ribos/vm/include/ribos/vm/handles.h \
	language/ribos/vm/include/ribos/vm/helpers.h \
	language/ribos/vm/include/ribos/vm/interpreter.h \
	language/ribos/vm/include/ribos/vm/terminal.h \
	language/ribos/vm/include/ribos/vm/verifier.h \
	language/ribos/vm/src/prepared_internal.h \
	language/ribos/vm/src/runtime/storage_internal.h \
	language/ribos/vm/src/runtime/helpers_internal.h \
	language/ribos/frontend/src/parser_internal.h \
	language/ribos/frontend/src/semantic_internal.h \
	language/ribos/frontend/generated/tokens.h

# Header ABI hard cuts must rebuild every previously emitted object on the next make run.
-include $(shell find "$(BUILD_ROOT)" -type f -name '*.d' -print 2>/dev/null)

CORE_SRCS := \
	src/core/arena.c \
	src/core/context.c \
	src/core/plugin.c \
	src/core/registry.c \
	src/core/service_directory.c
BOOT_LIB_SRCS := \
	src/core/memory.c \
	src/config/boot_config.c \
	src/filesystems/fat32.c \
	src/common/environment.c \
	src/common/module_bundle.c \
	src/common/protocol.c \
	src/common/boot.c \
	src/common/image.c \
	src/common/port.c \
	src/storage/block.c \
	src/storage/gpt.c
SDK_LIB_SRCS := \
	src/plugins/sdk.c \
	src/firmware/personality.c
ARCH_SRCS := \
	src/arch/common.c \
	src/arch/$(RIBON_ARCH)/arch.c
HOST_PRODUCT_SRCS := \
	src/environments/host/services.c \
	src/environments/host/ribos_policy.c \
	src/protocols/synthetic/protocol.c \
	src/image-formats/elf64.c \
	src/modes/normal.c
HOST_MAIN_SRC := src/environments/host/main.c

CORE_OBJS := $(CORE_SRCS:%.c=$(BUILD_DIR)/obj/%.o)
BOOT_LIB_OBJS := $(BOOT_LIB_SRCS:%.c=$(BUILD_DIR)/obj/%.o)
SDK_LIB_OBJS := $(SDK_LIB_SRCS:%.c=$(BUILD_DIR)/obj/%.o)
ARCH_OBJS := $(ARCH_SRCS:%.c=$(BUILD_DIR)/obj/%.o)
HOST_PRODUCT_OBJS := $(HOST_PRODUCT_SRCS:%.c=$(BUILD_DIR)/obj/%.o)
HOST_MAIN_OBJ := $(BUILD_DIR)/obj/$(HOST_MAIN_SRC:.c=.o)

LOADER_TEST := $(TEST_BUILD_DIR)/elf64_loader_tests
PE_COFF_TEST := $(TEST_BUILD_DIR)/pe_coff_loader_tests
FDT_TEST := $(TEST_BUILD_DIR)/fdt_parser_tests
RAW_FDT_CAPACITY_TEST := $(TEST_BUILD_DIR)/raw_fdt_capacity_tests
RPH1_TEST := $(TEST_BUILD_DIR)/rph1_builder_tests
ARCH_X86_64_TEST := $(TEST_BUILD_DIR)/x86_64_direct_high_tests
ARCH_AARCH64_TEST := $(TEST_BUILD_DIR)/aarch64_direct_high_tests
ARCH_OPS_TESTS := $(RIBON_ARCHES:%=$(TEST_BUILD_DIR)/arch_ops_%_tests)
CORE_SERVICE_TEST := $(TEST_BUILD_DIR)/core_service_boundary_tests
PORT_SERVICE_TEST := $(TEST_BUILD_DIR)/port_service_tests
BOOT_LIFECYCLE_TEST := $(TEST_BUILD_DIR)/boot_lifecycle_tests
ENVIRONMENT_PERSISTENT_INPUTS_TEST := $(TEST_BUILD_DIR)/environment_persistent_inputs_tests
BOOT_MODULE_BUNDLE_TEST := $(TEST_BUILD_DIR)/boot_module_bundle_tests
MEDIA_PIPELINE_TEST := $(TEST_BUILD_DIR)/media_pipeline_tests
PLUGIN_DESCRIPTOR_TEST := $(TEST_BUILD_DIR)/plugin_descriptor_tests
PROTOCOL_CONTRACT_TEST := $(TEST_BUILD_DIR)/protocol_contract_tests
PARUS_ENTRY_CONTRACT_TEST := $(TEST_BUILD_DIR)/parus_entry_contract_tests
OS_PACKAGE_TEST := $(TEST_BUILD_DIR)/os_package_tests
LINUX_BOOT_TEST := $(TEST_BUILD_DIR)/linux_boot_tests
PROTOCOL_FREE_EMBED_TEST := $(TEST_BUILD_DIR)/protocol_free_embed_tests
SDK_INSTALL_ROOT := $(BUILD_ROOT)/sdk/install
SDK_REPRO_FIRST := $(BUILD_ROOT)/sdk/reproducible-a
SDK_REPRO_SECOND := $(BUILD_ROOT)/sdk/reproducible-b
SDK_LIBRARY_EMBED_TEST := $(BUILD_ROOT)/sdk/examples/library-embed
SDK_DEPLOYMENT_CONSUMER_DIR := $(BUILD_ROOT)/sdk/examples/deployment-consumer
SDK_DEPLOYMENT_CONSUMER_REPORT := \
	$(SDK_DEPLOYMENT_CONSUMER_DIR)/results/consumer.json
EXTERNAL_PLUGIN_DIR := $(BUILD_ROOT)/sdk/examples/diagnostic-sink
EXTERNAL_PLUGIN_REGISTRY_C := $(EXTERNAL_PLUGIN_DIR)/generated/plugin_registry.c
EXTERNAL_PLUGIN_REPORT := $(EXTERNAL_PLUGIN_DIR)/results/object-graph.json
EXTERNAL_PLUGIN_TEST := $(EXTERNAL_PLUGIN_DIR)/external-diagnostic-sink-contract
EXTERNAL_PLUGIN_ROOT := examples/plugins/diagnostic-sink
MODE_DESCRIPTOR_TESTS := \
	$(TEST_BUILD_DIR)/mode_descriptor_normal_tests \
	$(TEST_BUILD_DIR)/mode_descriptor_recovery_tests \
	$(TEST_BUILD_DIR)/mode_descriptor_provisioning_tests \
	$(TEST_BUILD_DIR)/mode_descriptor_diagnostic_tests

RAW_IMAGE_FORMAT_SRCS ?= src/image-formats/elf64.c
RAW_PROTOCOL_SRCS ?= \
	src/protocols/os/parus/protocol.c \
	src/protocols/os/parus/rph1_builder.c \
	src/protocols/os/parus/rph1_parser.c

RAW_COMMON_SRCS := \
	src/core/arena.c \
	src/core/context.c \
	src/core/plugin.c \
	src/core/registry.c \
	src/core/service_directory.c \
	src/core/memory.c \
	src/common/environment.c \
	src/common/protocol.c \
	src/common/boot.c \
	src/common/image.c \
	src/common/port.c \
	src/common/freestanding/string.c \
	src/common/sys/fdt/fdt.c \
	src/arch/common.c \
	src/modes/normal.c \
	$(RAW_IMAGE_FORMAT_SRCS) \
	$(RAW_PROTOCOL_SRCS) \
	src/environments/raw-fdt/raw_fdt.c \
	products/bootmgr/raw_fdt_main.c

QEMU_RAW_DIR := $(TARGET_BUILD_ROOT)/qemu-aarch64-virt-raw-fdt
QEMU_RAW_MANIFEST := products/bootmgr/manifests/qemu-aarch64-virt-parus.json
QEMU_RAW_REGISTRY_C := $(QEMU_RAW_DIR)/generated/plugin_registry.c
QEMU_RAW_GRAPH := $(QEMU_RAW_DIR)/results/object-graph.json
QEMU_RAW_FIXTURE := $(QEMU_RAW_DIR)/payload.elf
QEMU_RAW_PAYLOAD ?= $(QEMU_RAW_FIXTURE)
QEMU_RAW_EMBED_C := $(QEMU_RAW_DIR)/generated/embedded_payload.c
QEMU_RAW_EXTERNAL_PAYLOAD_ASM ?=
QEMU_RAW_CPPFLAGS ?=
QEMU_RAW_ELF := $(QEMU_RAW_DIR)/ribon.elf
QEMU_RAW_IMAGE := $(QEMU_RAW_DIR)/ribon.bin
QEMU_RAW_SRCS := $(RAW_COMMON_SRCS) \
	src/common/drivers/serial/pl011.c \
	src/arch/aarch64/arch.c \
	ports/qemu/virt-aarch64/port.c
QEMU_RAW_OBJS := $(QEMU_RAW_SRCS:%.c=$(QEMU_RAW_DIR)/obj/%.o)
QEMU_RAW_OBJS += \
	$(QEMU_RAW_DIR)/obj/generated/plugin_registry.o \
	$(QEMU_RAW_DIR)/obj/targets/qemu-aarch64-virt-raw-fdt/entry.o
ifneq ($(strip $(QEMU_RAW_EXTERNAL_PAYLOAD_ASM)),)
QEMU_RAW_OBJS += $(QEMU_RAW_DIR)/obj/generated/external_payload.o
else
QEMU_RAW_OBJS += $(QEMU_RAW_DIR)/obj/generated/embedded_payload.o
endif

QEMU_PARUS_DIR := $(TARGET_BUILD_ROOT)/qemu-aarch64-virt-parus
QEMU_PARUS_MANIFEST := products/bootmgr/manifests/qemu-aarch64-virt-parus-external.json
QEMU_PARUS_PAYLOAD ?=
QEMU_PARUS_IMAGE := $(QEMU_PARUS_DIR)/ribon.bin
QEMU_PARUS_VALIDATION := $(QEMU_PARUS_DIR)/results/external-payload.json

QEMU_MODULE_FIXTURE_DIR := \
	$(TARGET_BUILD_ROOT)/qemu-aarch64-virt-modules-fixture
QEMU_MODULE_FIXTURE_MANIFEST := \
	products/bootmgr/manifests/qemu-aarch64-virt-modules-fixture.json
QEMU_MODULE_FIXTURE_COMPONENT_MANIFEST := \
	tests/fixtures/boot-modules/manifest.json
QEMU_MODULE_FIXTURE_IMAGE := $(QEMU_MODULE_FIXTURE_DIR)/ribon.bin
QEMU_MODULE_FIXTURE_PAYLOAD := $(QEMU_MODULE_FIXTURE_DIR)/payload.elf
QEMU_MODULE_FIXTURE_PROVENANCE := \
	$(QEMU_MODULE_FIXTURE_DIR)/results/boot-modules.json

QEMU_PARUS_MODULE_DIR := $(TARGET_BUILD_ROOT)/qemu-aarch64-virt-parus-modules
QEMU_PARUS_MODULE_PRODUCT_MANIFEST := \
	products/bootmgr/manifests/qemu-aarch64-virt-parus-modules.json
QEMU_PARUS_MODULE_COMPONENT_MANIFEST ?=
QEMU_PARUS_MODULE_IMAGE := $(QEMU_PARUS_MODULE_DIR)/ribon.bin
QEMU_PARUS_MODULE_VALIDATION := \
	$(QEMU_PARUS_MODULE_DIR)/results/external-payload.json
QEMU_PARUS_MODULE_PROVENANCE := \
	$(QEMU_PARUS_MODULE_DIR)/results/boot-modules.json

QEMU_LINUX_DIR := $(TARGET_BUILD_ROOT)/qemu-aarch64-virt-linux
QEMU_LINUX_MANIFEST := products/bootmgr/manifests/qemu-aarch64-virt-linux.json
QEMU_LINUX_INPUT_DESCRIPTOR := \
	external/inputs/linux-aarch64-openwrt-23.05.3.json
QEMU_LINUX_CACHE ?= $(BUILD_ROOT)/external/linux/openwrt-23.05.3-aarch64/Image
QEMU_LINUX_EXTERNAL_ASM := $(QEMU_LINUX_DIR)/generated/external_payload.S
QEMU_LINUX_EXTERNAL_VALIDATION := \
	$(QEMU_LINUX_DIR)/results/external-linux-image.json
QEMU_LINUX_EXTERNAL_STAMP := $(QEMU_LINUX_DIR)/generated/external-linux-image.stamp
QEMU_LINUX_INIT_OBJ := $(QEMU_LINUX_DIR)/initramfs/init.o
QEMU_LINUX_INIT_ELF := $(QEMU_LINUX_DIR)/initramfs/init
QEMU_LINUX_INITRAMFS := $(QEMU_LINUX_DIR)/initramfs/initramfs.cpio
QEMU_LINUX_MODULE_MANIFEST := $(QEMU_LINUX_DIR)/initramfs/manifest.json
QEMU_LINUX_INITRAMFS_STAMP := $(QEMU_LINUX_DIR)/initramfs/generated.stamp
QEMU_LINUX_IMAGE := $(QEMU_LINUX_DIR)/ribon.bin
QEMU_LINUX_MODULE_PROVENANCE := $(QEMU_LINUX_DIR)/results/boot-modules.json

QEMU_RAW_MODULE_COMPONENT_MANIFEST ?=
ifneq ($(strip $(QEMU_RAW_MODULE_COMPONENT_MANIFEST)),)
QEMU_RAW_MODULE_DIR := $(QEMU_RAW_DIR)/generated/boot-modules
QEMU_RAW_MODULE_ASM := $(QEMU_RAW_MODULE_DIR)/bundle.S
QEMU_RAW_MODULE_C := $(QEMU_RAW_MODULE_DIR)/descriptor.c
QEMU_RAW_MODULE_STAMP := $(QEMU_RAW_MODULE_DIR)/generated.stamp
QEMU_RAW_MODULE_PROVENANCE := $(QEMU_RAW_DIR)/results/boot-modules.json
QEMU_RAW_MODULE_CPPFLAGS := -DRIBON_RAW_FDT_HAS_BOOT_MODULE_BUNDLE=1
QEMU_RAW_OBJS += \
	$(QEMU_RAW_DIR)/obj/src/common/module_bundle.o \
	$(QEMU_RAW_DIR)/obj/generated/boot-modules/descriptor.o \
	$(QEMU_RAW_DIR)/obj/generated/boot-modules/bundle.o
endif

QEMU_RISCV64_DIR := $(TARGET_BUILD_ROOT)/qemu-riscv64-virt-opensbi
QEMU_RISCV64_MANIFEST := products/bootmgr/manifests/qemu-riscv64-virt-parus-external.json
QEMU_RISCV64_REGISTRY_C := $(QEMU_RISCV64_DIR)/generated/plugin_registry.c
QEMU_RISCV64_GRAPH := $(QEMU_RISCV64_DIR)/results/object-graph.json
QEMU_RISCV64_PAYLOAD ?=
QEMU_RISCV64_EMBED_C := $(QEMU_RISCV64_DIR)/generated/embedded_payload.c
QEMU_RISCV64_EXTERNAL_PAYLOAD_ASM ?=
QEMU_RISCV64_CPPFLAGS ?=
QEMU_RISCV64_ELF := $(QEMU_RISCV64_DIR)/ribon.elf
QEMU_RISCV64_IMAGE := $(QEMU_RISCV64_DIR)/ribon.bin
QEMU_RISCV64_VALIDATION := $(QEMU_RISCV64_DIR)/results/external-payload.json
QEMU_RISCV64_SRCS := $(RAW_COMMON_SRCS) \
	src/arch/riscv64/arch.c \
	ports/qemu/virt-riscv64/port.c
QEMU_RISCV64_OBJS := $(QEMU_RISCV64_SRCS:%.c=$(QEMU_RISCV64_DIR)/obj/%.o)
QEMU_RISCV64_OBJS += \
	$(QEMU_RISCV64_DIR)/obj/generated/plugin_registry.o \
	$(QEMU_RISCV64_DIR)/obj/targets/qemu-riscv64-virt-opensbi/entry.o
ifneq ($(strip $(QEMU_RISCV64_EXTERNAL_PAYLOAD_ASM)),)
QEMU_RISCV64_OBJS += $(QEMU_RISCV64_DIR)/obj/generated/external_payload.o
else
QEMU_RISCV64_OBJS += $(QEMU_RISCV64_DIR)/obj/generated/embedded_payload.o
endif

QEMU_RISCV64_MODULE_COMPONENT_MANIFEST ?=
ifneq ($(strip $(QEMU_RISCV64_MODULE_COMPONENT_MANIFEST)),)
QEMU_RISCV64_MODULE_DIR := $(QEMU_RISCV64_DIR)/generated/boot-modules
QEMU_RISCV64_MODULE_ASM := $(QEMU_RISCV64_MODULE_DIR)/bundle.S
QEMU_RISCV64_MODULE_C := $(QEMU_RISCV64_MODULE_DIR)/descriptor.c
QEMU_RISCV64_MODULE_STAMP := $(QEMU_RISCV64_MODULE_DIR)/generated.stamp
QEMU_RISCV64_MODULE_PROVENANCE := \
	$(QEMU_RISCV64_DIR)/results/boot-modules.json
QEMU_RISCV64_MODULE_CPPFLAGS := -DRIBON_RAW_FDT_HAS_BOOT_MODULE_BUNDLE=1
QEMU_RISCV64_OBJS += \
	$(QEMU_RISCV64_DIR)/obj/src/common/module_bundle.o \
	$(QEMU_RISCV64_DIR)/obj/generated/boot-modules/descriptor.o \
	$(QEMU_RISCV64_DIR)/obj/generated/boot-modules/bundle.o
endif

QEMU_RISCV64_LINUX_DIR := \
	$(TARGET_BUILD_ROOT)/qemu-riscv64-virt-linux
QEMU_RISCV64_LINUX_MANIFEST := \
	products/bootmgr/manifests/qemu-riscv64-virt-linux.json
QEMU_RISCV64_LINUX_INPUT_DESCRIPTOR := \
	external/inputs/linux-riscv64-debian-13-installer-20250803-deb13u6.json
QEMU_RISCV64_LINUX_CACHE ?= \
	$(BUILD_ROOT)/external/linux/debian-trixie-riscv64/Image
QEMU_RISCV64_LINUX_EXTERNAL_ASM := \
	$(QEMU_RISCV64_LINUX_DIR)/generated/external_payload.S
QEMU_RISCV64_LINUX_EXTERNAL_VALIDATION := \
	$(QEMU_RISCV64_LINUX_DIR)/results/external-linux-image.json
QEMU_RISCV64_LINUX_EXTERNAL_STAMP := \
	$(QEMU_RISCV64_LINUX_DIR)/generated/external-linux-image.stamp
QEMU_RISCV64_LINUX_INIT_OBJ := \
	$(QEMU_RISCV64_LINUX_DIR)/initramfs/init.o
QEMU_RISCV64_LINUX_INIT_ELF := \
	$(QEMU_RISCV64_LINUX_DIR)/initramfs/init
QEMU_RISCV64_LINUX_INITRAMFS := \
	$(QEMU_RISCV64_LINUX_DIR)/initramfs/initramfs.cpio
QEMU_RISCV64_LINUX_MODULE_MANIFEST := \
	$(QEMU_RISCV64_LINUX_DIR)/initramfs/manifest.json
QEMU_RISCV64_LINUX_INITRAMFS_STAMP := \
	$(QEMU_RISCV64_LINUX_DIR)/initramfs/generated.stamp
QEMU_RISCV64_LINUX_IMAGE := $(QEMU_RISCV64_LINUX_DIR)/ribon.bin
QEMU_RISCV64_LINUX_MODULE_PROVENANCE := \
	$(QEMU_RISCV64_LINUX_DIR)/results/boot-modules.json

QEMU_RISCV64_RPH1_FIXTURE_DIR := \
	$(TARGET_BUILD_ROOT)/qemu-riscv64-virt-rph1-fixture
QEMU_RISCV64_RPH1_FIXTURE_MANIFEST := \
	products/bootmgr/manifests/qemu-riscv64-virt-rph1-fixture.json
QEMU_RISCV64_RPH1_FIXTURE_PAYLOAD := \
	$(QEMU_RISCV64_RPH1_FIXTURE_DIR)/payload.elf
QEMU_RISCV64_RPH1_FIXTURE_IMAGE := \
	$(QEMU_RISCV64_RPH1_FIXTURE_DIR)/ribon.bin
QEMU_RISCV64_RPH1_FIXTURE_OBJS := \
	$(QEMU_RISCV64_RPH1_FIXTURE_DIR)/obj/tests/fixtures/riscv64/rph1_consumer.o \
	$(QEMU_RISCV64_RPH1_FIXTURE_DIR)/obj/tests/fixtures/riscv64/rph1_consumer_entry.o

RPI5_DIR := $(TARGET_BUILD_ROOT)/rpi5-aarch64-raw-fdt
RPI5_MANIFEST := products/bootmgr/manifests/rpi5-aarch64-parus.json
RPI5_EXTERNAL_MANIFEST := products/bootmgr/manifests/rpi5-aarch64-parus-external.json
RPI5_PARUS_PAYLOAD ?=
RPI5_PARUS_VALIDATION := $(RPI5_DIR)/results/external-payload.json
RPI5_REGISTRY_C := $(RPI5_DIR)/generated/plugin_registry.c
RPI5_GRAPH := $(RPI5_DIR)/results/object-graph.json
RPI5_FIXTURE := $(RPI5_DIR)/payload.elf
ifneq ($(strip $(RPI5_PARUS_PAYLOAD)),)
RPI5_SELECTED_MANIFEST := $(RPI5_EXTERNAL_MANIFEST)
RPI5_SELECTED_PAYLOAD := $(abspath $(RPI5_PARUS_PAYLOAD))
RPI5_SELECTED_VALIDATION := $(RPI5_PARUS_VALIDATION)
else
RPI5_SELECTED_MANIFEST := $(RPI5_MANIFEST)
RPI5_SELECTED_PAYLOAD := $(RPI5_FIXTURE)
RPI5_SELECTED_VALIDATION :=
endif
RPI5_EMBED_C := $(RPI5_DIR)/generated/embedded_payload.c
RPI5_ELF := $(RPI5_DIR)/ribon.elf
RPI5_IMAGE := $(RPI5_DIR)/ribon-rpi5.img
RPI5_PACKAGE := $(RPI5_DIR)/package
RPI5_SRCS := $(RAW_COMMON_SRCS) \
	src/common/drivers/serial/pl011.c \
	src/arch/aarch64/arch.c \
	ports/raspberrypi/rpi5/port.c
RPI5_OBJS := $(RPI5_SRCS:%.c=$(RPI5_DIR)/obj/%.o)
RPI5_OBJS += \
	$(RPI5_DIR)/obj/generated/plugin_registry.o \
	$(RPI5_DIR)/obj/generated/embedded_payload.o \
	$(RPI5_DIR)/obj/targets/rpi5-aarch64-raw-fdt/entry.o

RPI5_MODULE_COMPONENT_MANIFEST ?=
ifneq ($(strip $(RPI5_MODULE_COMPONENT_MANIFEST)),)
RPI5_MODULE_DIR := $(RPI5_DIR)/generated/boot-modules
RPI5_MODULE_ASM := $(RPI5_MODULE_DIR)/bundle.S
RPI5_MODULE_C := $(RPI5_MODULE_DIR)/descriptor.c
RPI5_MODULE_STAMP := $(RPI5_MODULE_DIR)/generated.stamp
RPI5_MODULE_PROVENANCE := $(RPI5_DIR)/results/boot-modules.json
RPI5_MODULE_CPPFLAGS := -DRIBON_RAW_FDT_HAS_BOOT_MODULE_BUNDLE=1
RPI5_OBJS += \
	$(RPI5_DIR)/obj/src/common/module_bundle.o \
	$(RPI5_DIR)/obj/generated/boot-modules/descriptor.o \
	$(RPI5_DIR)/obj/generated/boot-modules/bundle.o
endif

RPI5_MODULE_FIXTURE_DIR := $(TARGET_BUILD_ROOT)/rpi5-aarch64-modules-fixture
RPI5_MODULE_FIXTURE_MANIFEST := \
	products/bootmgr/manifests/rpi5-aarch64-modules-fixture.json
RPI5_MODULE_FIXTURE_COMPONENT_MANIFEST := \
	tests/fixtures/boot-modules/manifest.json
RPI5_MODULE_FIXTURE_PACKAGE := $(RPI5_MODULE_FIXTURE_DIR)/package
RPI5_PARUS_MODULE_DIR := $(TARGET_BUILD_ROOT)/rpi5-aarch64-parus-modules
RPI5_PARUS_MODULE_MANIFEST := \
	products/bootmgr/manifests/rpi5-aarch64-parus-modules.json
RPI5_PARUS_MODULE_COMPONENT_MANIFEST ?=
RPI5_PARUS_MODULE_PACKAGE := $(RPI5_PARUS_MODULE_DIR)/package

UEFI_FIXTURE_PRODUCT := x86_64-uefi-parus-fixture
UEFI_EXTERNAL_PRODUCT := x86_64-uefi-parus-external
UEFI_FIXTURE_DIR := $(TARGET_BUILD_ROOT)/$(UEFI_FIXTURE_PRODUCT)
UEFI_EXTERNAL_DIR := $(TARGET_BUILD_ROOT)/$(UEFI_EXTERNAL_PRODUCT)
UEFI_FIXTURE_MANIFEST := \
	products/bootmgr/manifests/x86_64-uefi-parus-fixture.json
UEFI_EXTERNAL_MANIFEST := \
	products/bootmgr/manifests/x86_64-uefi-parus-external.json
UEFI_PARUS_PAYLOAD ?=
UEFI_EXTERNAL_SOURCE := $(if $(strip $(UEFI_PARUS_PAYLOAD)),\
	$(abspath $(UEFI_PARUS_PAYLOAD)))

UEFI_FIXTURE_REGISTRY_C := $(UEFI_FIXTURE_DIR)/generated/plugin_registry.c
UEFI_FIXTURE_GRAPH := $(UEFI_FIXTURE_DIR)/results/object-graph.json
UEFI_FIXTURE_INPUT_MANIFEST := $(UEFI_FIXTURE_DIR)/manifests/product.json
UEFI_FIXTURE_PAYLOAD_SOURCE := $(UEFI_FIXTURE_DIR)/fixtures/payload.elf
UEFI_FIXTURE_APP := $(UEFI_FIXTURE_DIR)/BOOTX64.EFI
UEFI_FIXTURE_ESP := $(UEFI_FIXTURE_DIR)/esp
UEFI_FIXTURE_CONFIG := $(UEFI_FIXTURE_ESP)/RIBON/BOOT.CFG
UEFI_FIXTURE_PAYLOAD := $(UEFI_FIXTURE_ESP)/RIBON/PAYLOAD.ELF
UEFI_FIXTURE_INIT_SOURCE := $(UEFI_FIXTURE_DIR)/fixtures/init-image.bin
UEFI_FIXTURE_INIT_IMAGE := $(UEFI_FIXTURE_ESP)/RIBON/INIT.IMG

UEFI_EXTERNAL_REGISTRY_C := $(UEFI_EXTERNAL_DIR)/generated/plugin_registry.c
UEFI_EXTERNAL_GRAPH := $(UEFI_EXTERNAL_DIR)/results/object-graph.json
UEFI_EXTERNAL_INPUT_MANIFEST := $(UEFI_EXTERNAL_DIR)/manifests/product.json
UEFI_EXTERNAL_VALIDATION := $(UEFI_EXTERNAL_DIR)/results/external-payload.json
UEFI_EXTERNAL_APP := $(UEFI_EXTERNAL_DIR)/BOOTX64.EFI
UEFI_EXTERNAL_ESP := $(UEFI_EXTERNAL_DIR)/esp
UEFI_EXTERNAL_CONFIG := $(UEFI_EXTERNAL_ESP)/RIBON/BOOT.CFG
UEFI_EXTERNAL_PAYLOAD := $(UEFI_EXTERNAL_ESP)/RIBON/PAYLOAD.ELF
UEFI_EXTERNAL_INIT_SOURCE := $(UEFI_EXTERNAL_DIR)/fixtures/init-image.bin
UEFI_EXTERNAL_INIT_IMAGE := $(UEFI_EXTERNAL_ESP)/RIBON/INIT.IMG
UEFI_SRCS := \
	src/core/arena.c \
	src/core/context.c \
	src/core/plugin.c \
	src/core/registry.c \
	src/core/service_directory.c \
	src/core/memory.c \
	src/config/boot_config.c \
	src/common/environment.c \
	src/common/protocol.c \
	src/common/boot.c \
	src/common/image.c \
	src/common/port.c \
	src/common/freestanding/string.c \
	src/arch/common.c \
	src/arch/x86_64/arch.c \
	src/arch/x86_64/io.c \
	src/modes/normal.c \
	src/image-formats/elf64.c \
	src/protocols/os/parus/protocol.c \
	src/protocols/os/parus/rph1_builder.c \
	src/protocols/os/parus/rph1_parser.c \
	src/environments/uefi-app/uefi_app.c \
	ports/qemu/pc-x86_64/port.c \
	targets/x86_64-uefi-app/entry.c
UEFI_FIXTURE_OBJS := $(UEFI_SRCS:%.c=$(UEFI_FIXTURE_DIR)/obj/%.o)
UEFI_FIXTURE_OBJS += \
	$(UEFI_FIXTURE_DIR)/obj/generated/plugin_registry.o
UEFI_EXTERNAL_OBJS := $(UEFI_SRCS:%.c=$(UEFI_EXTERNAL_DIR)/obj/%.o)
UEFI_EXTERNAL_OBJS += \
	$(UEFI_EXTERNAL_DIR)/obj/generated/plugin_registry.o

UEFI_LINUX_PRODUCT := x86_64-uefi-linux
UEFI_LINUX_DIR := $(TARGET_BUILD_ROOT)/$(UEFI_LINUX_PRODUCT)
UEFI_LINUX_MANIFEST := products/bootmgr/manifests/x86_64-uefi-linux.json
UEFI_LINUX_INPUT_DESCRIPTOR := external/inputs/linux-x86_64-openwrt-24.10.0.json
UEFI_LINUX_CACHE := $(BUILD_ROOT)/external/linux/openwrt-24.10.0-x86_64/bzImage.efi
UEFI_LINUX_EXTERNAL_VALIDATION := $(UEFI_LINUX_DIR)/results/external-linux-efi.json
UEFI_LINUX_EXTERNAL_STAMP := $(UEFI_LINUX_DIR)/external-linux-efi.stamp
UEFI_LINUX_REGISTRY_C := $(UEFI_LINUX_DIR)/generated/plugin_registry.c
UEFI_LINUX_GRAPH := $(UEFI_LINUX_DIR)/results/object-graph.json
UEFI_LINUX_INPUT_MANIFEST := $(UEFI_LINUX_DIR)/manifests/product.json
UEFI_LINUX_APP := $(UEFI_LINUX_DIR)/BOOTX64.EFI
UEFI_LINUX_ESP := $(UEFI_LINUX_DIR)/esp
UEFI_LINUX_CONFIG := $(UEFI_LINUX_ESP)/RIBON/BOOT.CFG
UEFI_LINUX_PAYLOAD := $(UEFI_LINUX_ESP)/RIBON/LINUX.EFI
UEFI_LINUX_INIT_OBJ := $(UEFI_LINUX_DIR)/fixtures/init.o
UEFI_LINUX_INIT_ELF := $(UEFI_LINUX_DIR)/fixtures/init
UEFI_LINUX_INITRAMFS := $(UEFI_LINUX_ESP)/RIBON/INITRD.CPIO
UEFI_LINUX_INITRAMFS_MANIFEST := $(UEFI_LINUX_DIR)/results/initramfs-components.json
UEFI_LINUX_INITRAMFS_STAMP := $(UEFI_LINUX_DIR)/initramfs.stamp
UEFI_LINUX_SRCS := \
	src/core/arena.c \
	src/core/context.c \
	src/core/plugin.c \
	src/core/registry.c \
	src/core/service_directory.c \
	src/core/memory.c \
	src/config/boot_config.c \
	src/common/environment.c \
	src/common/protocol.c \
	src/common/boot.c \
	src/common/image.c \
	src/common/port.c \
	src/common/freestanding/string.c \
	src/arch/common.c \
	src/arch/x86_64/arch.c \
	src/arch/x86_64/io.c \
	src/modes/normal.c \
	src/image-formats/pe_coff.c \
	src/protocols/os/linux/efi.c \
	src/environments/uefi-app/uefi_app.c \
	src/environments/uefi-app/terminal_image.c \
	ports/qemu/pc-x86_64/port.c \
	targets/x86_64-uefi-app/entry.c
UEFI_LINUX_OBJS := $(UEFI_LINUX_SRCS:%.c=$(UEFI_LINUX_DIR)/obj/%.o)
UEFI_LINUX_OBJS += $(UEFI_LINUX_DIR)/obj/generated/plugin_registry.o

UEFI_FREEBSD_PRODUCT := x86_64-uefi-freebsd
UEFI_FREEBSD_DIR := $(TARGET_BUILD_ROOT)/$(UEFI_FREEBSD_PRODUCT)
UEFI_FREEBSD_MANIFEST := products/bootmgr/manifests/x86_64-uefi-freebsd.json
UEFI_FREEBSD_INPUT_DESCRIPTOR := external/inputs/freebsd-amd64-15.1-release.json
UEFI_FREEBSD_CACHE_DIR := $(BUILD_ROOT)/external/freebsd/15.1-amd64
UEFI_FREEBSD_COMPRESSED_CACHE := \
	$(UEFI_FREEBSD_CACHE_DIR)/FreeBSD-15.1-RELEASE-amd64-mini-memstick.img.xz
UEFI_FREEBSD_RAW_CACHE := \
	$(UEFI_FREEBSD_CACHE_DIR)/FreeBSD-15.1-RELEASE-amd64-mini-memstick.img
UEFI_FREEBSD_EXTERNAL_VALIDATION := \
	$(UEFI_FREEBSD_DIR)/results/external-freebsd.json
UEFI_FREEBSD_EXTERNAL_STAMP := $(UEFI_FREEBSD_DIR)/external-freebsd.stamp
UEFI_FREEBSD_REGISTRY_C := $(UEFI_FREEBSD_DIR)/generated/plugin_registry.c
UEFI_FREEBSD_GRAPH := $(UEFI_FREEBSD_DIR)/results/object-graph.json
UEFI_FREEBSD_INPUT_MANIFEST := $(UEFI_FREEBSD_DIR)/manifests/product.json
UEFI_FREEBSD_APP := $(UEFI_FREEBSD_DIR)/BOOTX64.EFI
UEFI_FREEBSD_CONFIG := $(UEFI_FREEBSD_DIR)/overlay/BOOT.CFG
UEFI_FREEBSD_DISK := $(UEFI_FREEBSD_DIR)/FreeBSD-15.1-Ribon-amd64.img
UEFI_FREEBSD_LOADER := $(UEFI_FREEBSD_DIR)/payload/loader.efi
UEFI_FREEBSD_PACKAGE := $(UEFI_FREEBSD_DIR)/results/package.json
UEFI_FREEBSD_PACKAGE_STAMP := $(UEFI_FREEBSD_DIR)/package.stamp
UEFI_FREEBSD_SRCS := \
	src/core/arena.c \
	src/core/context.c \
	src/core/plugin.c \
	src/core/registry.c \
	src/core/service_directory.c \
	src/core/memory.c \
	src/config/boot_config.c \
	src/common/environment.c \
	src/common/protocol.c \
	src/common/boot.c \
	src/common/image.c \
	src/common/port.c \
	src/common/freestanding/string.c \
	src/arch/common.c \
	src/arch/x86_64/arch.c \
	src/arch/x86_64/io.c \
	src/modes/normal.c \
	src/image-formats/pe_coff.c \
	src/protocols/os/freebsd/protocol.c \
	src/environments/uefi-app/uefi_app.c \
	src/environments/uefi-app/terminal_image.c \
	ports/qemu/pc-x86_64/port.c \
	targets/x86_64-uefi-app/entry.c
UEFI_FREEBSD_OBJS := $(UEFI_FREEBSD_SRCS:%.c=$(UEFI_FREEBSD_DIR)/obj/%.o)
UEFI_FREEBSD_OBJS += $(UEFI_FREEBSD_DIR)/obj/generated/plugin_registry.o

UEFI_UPDATE_PRODUCT := x86_64-uefi-update-recovery
UEFI_UPDATE_DIR := $(TARGET_BUILD_ROOT)/$(UEFI_UPDATE_PRODUCT)
UEFI_UPDATE_MANIFEST := \
	products/validation/manifests/x86_64-uefi-update-recovery.json
UEFI_UPDATE_REGISTRY_C := $(UEFI_UPDATE_DIR)/generated/plugin_registry.c
UEFI_UPDATE_GRAPH := $(UEFI_UPDATE_DIR)/results/object-graph.json
UEFI_UPDATE_FIXTURE_DIR := $(UEFI_UPDATE_DIR)/fixture
UEFI_UPDATE_FIXTURE_STAMP := $(UEFI_UPDATE_FIXTURE_DIR)/generated.stamp
UEFI_UPDATE_DISK := $(UEFI_UPDATE_FIXTURE_DIR)/update-disk.raw
UEFI_UPDATE_FIXTURE_PROVENANCE := $(UEFI_UPDATE_FIXTURE_DIR)/provenance.json
UEFI_UPDATE_APP := $(UEFI_UPDATE_DIR)/BOOTX64.EFI
UEFI_UPDATE_ESP := $(UEFI_UPDATE_DIR)/esp
UEFI_UPDATE_RESULTS := $(UEFI_UPDATE_DIR)/results
UEFI_UPDATE_SRCS := \
	src/core/arena.c \
	src/core/context.c \
	src/core/plugin.c \
	src/core/registry.c \
	src/core/service_directory.c \
	src/core/memory.c \
	src/common/environment.c \
	src/common/protocol.c \
	src/common/boot.c \
	src/common/image.c \
	src/common/port.c \
	src/common/freestanding/string.c \
	src/arch/common.c \
	src/arch/x86_64/arch.c \
	src/arch/x86_64/io.c \
	src/modes/recovery.c \
	src/image-formats/elf64.c \
	products/validation/uefi-update-recovery/protocol.c \
	src/environments/uefi-app/uefi_app.c \
	src/environments/uefi-app/update_storage.c \
	src/update/manifest.c \
	src/update/storage.c \
	src/update/installer.c \
	src/update/transaction.c \
	src/update/confirmation.c \
	src/security/sha256.c \
	src/security/key_policy.c \
	src/security/protected_state.c \
	src/security/signature.c \
	src/plugins/security/ed25519/provider.c \
	third_party/monocypher/4.0.3/monocypher.c \
	third_party/monocypher/4.0.3/monocypher-ed25519.c \
	products/validation/uefi-update-recovery/protected_state.c \
	ports/qemu/pc-x86_64/port.c \
	targets/x86_64-uefi-update-recovery/entry.c
UEFI_UPDATE_OBJS := $(UEFI_UPDATE_SRCS:%.c=$(UEFI_UPDATE_DIR)/obj/%.o)
UEFI_UPDATE_OBJS += $(UEFI_UPDATE_DIR)/obj/generated/plugin_registry.o

UEFI_NETWORK_UPDATE_PRODUCT := x86_64-uefi-network-update-recovery
UEFI_NETWORK_UPDATE_DIR := $(TARGET_BUILD_ROOT)/$(UEFI_NETWORK_UPDATE_PRODUCT)
UEFI_NETWORK_UPDATE_MANIFEST := \
	products/validation/manifests/x86_64-uefi-network-update-recovery.json
UEFI_NETWORK_UPDATE_REGISTRY_C := \
	$(UEFI_NETWORK_UPDATE_DIR)/generated/plugin_registry.c
UEFI_NETWORK_UPDATE_GRAPH := \
	$(UEFI_NETWORK_UPDATE_DIR)/results/object-graph.json
UEFI_NETWORK_UPDATE_FIXTURE_DIR := $(UEFI_NETWORK_UPDATE_DIR)/fixture
UEFI_NETWORK_UPDATE_FIXTURE_STAMP := \
	$(UEFI_NETWORK_UPDATE_FIXTURE_DIR)/generated.stamp
UEFI_NETWORK_UPDATE_DISK := \
	$(UEFI_NETWORK_UPDATE_FIXTURE_DIR)/update-disk.raw
UEFI_NETWORK_UPDATE_FIXTURE_PROVENANCE := \
	$(UEFI_NETWORK_UPDATE_FIXTURE_DIR)/provenance.json
UEFI_NETWORK_UPDATE_APP := $(UEFI_NETWORK_UPDATE_DIR)/BOOTX64.EFI
UEFI_NETWORK_UPDATE_ESP := $(UEFI_NETWORK_UPDATE_DIR)/esp
UEFI_NETWORK_UPDATE_RESULTS := $(UEFI_NETWORK_UPDATE_DIR)/results
UEFI_NETWORK_UPDATE_SRCS := \
	src/core/arena.c \
	src/core/context.c \
	src/core/plugin.c \
	src/core/registry.c \
	src/core/service_directory.c \
	src/core/memory.c \
	src/common/environment.c \
	src/common/protocol.c \
	src/common/boot.c \
	src/common/image.c \
	src/common/port.c \
	src/common/freestanding/string.c \
	src/common/net/recovery.c \
	src/common/net/tftp.c \
	src/arch/common.c \
	src/arch/x86_64/arch.c \
	src/arch/x86_64/io.c \
	src/modes/recovery.c \
	src/image-formats/elf64.c \
	products/validation/uefi-update-recovery/protocol.c \
	src/environments/uefi-app/uefi_app.c \
	src/environments/uefi-app/update_storage.c \
	src/environments/uefi-app/recovery_network.c \
	src/update/manifest.c \
	src/update/storage.c \
	src/update/installer.c \
	src/update/transaction.c \
	src/security/sha256.c \
	src/security/key_policy.c \
	src/security/protected_state.c \
	src/security/signature.c \
	src/plugins/security/ed25519/provider.c \
	third_party/monocypher/4.0.3/monocypher.c \
	third_party/monocypher/4.0.3/monocypher-ed25519.c \
	products/validation/uefi-update-recovery/protected_state.c \
	ports/qemu/pc-x86_64/port.c \
	targets/x86_64-uefi-network-update-recovery/entry.c
UEFI_NETWORK_UPDATE_OBJS := \
	$(UEFI_NETWORK_UPDATE_SRCS:%.c=$(UEFI_NETWORK_UPDATE_DIR)/obj/%.o)
UEFI_NETWORK_UPDATE_OBJS += \
	$(UEFI_NETWORK_UPDATE_DIR)/obj/generated/plugin_registry.o

RIBOS_R18_DIR := $(TARGET_BUILD_ROOT)/ribos-r18
RIBOS_R18_MANIFEST := $(RIBOS_R18_DIR)/generated/product.json
RIBOS_R18_UNSIGNED_A := $(RIBOS_R18_DIR)/generated/policy-a.rba
RIBOS_R18_UNSIGNED_B := $(RIBOS_R18_DIR)/generated/policy-b.rba
RIBOS_R18_ARTIFACT := $(RIBOS_R18_DIR)/generated/policy-signed.rba
RIBOS_R18_ARTIFACT_B := $(RIBOS_R18_DIR)/generated/policy-signed-b.rba
RIBOS_R18_TRIAL_ARTIFACT := \
	$(RIBOS_R18_DIR)/generated/policy-trial-signed.rba
RIBOS_R18_TRIAL_ARTIFACT_B := \
	$(RIBOS_R18_DIR)/generated/policy-trial-signed-b.rba
RIBOS_R18_GOLDEN_SHA256 := \
	language/ribos/vm/tests/golden/aggregate_ownership-r18.sha256
RIBOS_R18_TRIAL_GOLDEN_SHA256 := \
	language/ribos/vm/tests/golden/aggregate_ownership-r19.sha256
RIBOS_R18_EMBED_C := $(RIBOS_R18_DIR)/generated/embedded_policy.c
RIBOS_R18_TRIAL_EMBED_C := \
	$(RIBOS_R18_DIR)/generated/embedded_trial_policy.c
RIBOS_R18_COMMON_SRCS := \
	src/core/arena.c \
	src/core/context.c \
	src/core/plugin.c \
	src/core/registry.c \
	src/core/service_directory.c \
	src/core/memory.c \
	src/common/environment.c \
	src/common/protocol.c \
	src/common/boot.c \
	src/common/image.c \
	src/common/port.c \
	src/common/freestanding/string.c \
	src/arch/common.c \
	src/modes/normal.c \
	src/image-formats/elf64.c \
	src/protocols/synthetic/protocol.c \
	src/plugins/policy/ribos/adapter.c \
	$(SECURITY_KEY_POLICY_SRCS) \
	$(SECURITY_PROTECTED_STATE_SRCS) \
	$(SECURITY_PROVIDER_SRCS) \
	products/validation/ribos-qemu/product.c \
	products/validation/ribos-qemu/main.c

RIBOS_R18_AARCH64_DIR := $(RIBOS_R18_DIR)/aarch64
RIBOS_R18_AARCH64_REGISTRY_C := \
	$(RIBOS_R18_AARCH64_DIR)/generated/plugin_registry.c
RIBOS_R18_AARCH64_GRAPH := \
	$(RIBOS_R18_AARCH64_DIR)/results/object-graph.json
RIBOS_R18_AARCH64_SRCS := $(RIBOS_R18_COMMON_SRCS) \
	src/common/drivers/serial/pl011.c \
	src/arch/aarch64/arch.c \
	ports/qemu/virt-aarch64/port.c
RIBOS_R18_AARCH64_OBJS := \
	$(RIBOS_R18_AARCH64_SRCS:%.c=$(RIBOS_R18_AARCH64_DIR)/obj/%.o) \
	$(RIBOS_R18_AARCH64_DIR)/obj/generated/plugin_registry.o \
	$(RIBOS_R18_AARCH64_DIR)/obj/generated/embedded_policy.o \
	$(RIBOS_R18_AARCH64_DIR)/obj/generated/embedded_trial_policy.o \
	$(RIBOS_R18_AARCH64_DIR)/obj/targets/qemu-aarch64-virt-raw-fdt/entry.o
RIBOS_R18_AARCH64_VM_OBJS := \
	$(RIBOS_TARGET_CORE_SRCS:%.c=$(RIBOS_R18_AARCH64_DIR)/obj/%.o)
RIBOS_R18_AARCH64_VM_LIB := \
	$(RIBOS_R18_AARCH64_DIR)/libribos-target-core.a
RIBOS_R18_AARCH64_ELF := $(RIBOS_R18_AARCH64_DIR)/ribon-ribos.elf
RIBOS_R18_AARCH64_IMAGE := $(RIBOS_R18_AARCH64_DIR)/ribon-ribos.bin

RIBOS_R18_RISCV64_DIR := $(RIBOS_R18_DIR)/riscv64
RIBOS_R18_RISCV64_REGISTRY_C := \
	$(RIBOS_R18_RISCV64_DIR)/generated/plugin_registry.c
RIBOS_R18_RISCV64_GRAPH := \
	$(RIBOS_R18_RISCV64_DIR)/results/object-graph.json
RIBOS_R18_RISCV64_SRCS := $(RIBOS_R18_COMMON_SRCS) \
	src/arch/riscv64/arch.c \
	ports/qemu/virt-riscv64/port.c
RIBOS_R18_RISCV64_OBJS := \
	$(RIBOS_R18_RISCV64_SRCS:%.c=$(RIBOS_R18_RISCV64_DIR)/obj/%.o) \
	$(RIBOS_R18_RISCV64_DIR)/obj/generated/plugin_registry.o \
	$(RIBOS_R18_RISCV64_DIR)/obj/generated/embedded_policy.o \
	$(RIBOS_R18_RISCV64_DIR)/obj/generated/embedded_trial_policy.o \
	$(RIBOS_R18_RISCV64_DIR)/obj/targets/qemu-riscv64-virt-opensbi/entry.o
RIBOS_R18_RISCV64_VM_OBJS := \
	$(RIBOS_TARGET_CORE_SRCS:%.c=$(RIBOS_R18_RISCV64_DIR)/obj/%.o)
RIBOS_R18_RISCV64_VM_LIB := \
	$(RIBOS_R18_RISCV64_DIR)/libribos-target-core.a
RIBOS_R18_RISCV64_ELF := $(RIBOS_R18_RISCV64_DIR)/ribon-ribos.elf
RIBOS_R18_RISCV64_IMAGE := $(RIBOS_R18_RISCV64_DIR)/ribon-ribos.bin

RIBOS_R18_AMD64_DIR := $(RIBOS_R18_DIR)/amd64
RIBOS_R18_AMD64_REGISTRY_C := \
	$(RIBOS_R18_AMD64_DIR)/generated/plugin_registry.c
RIBOS_R18_AMD64_GRAPH := \
	$(RIBOS_R18_AMD64_DIR)/results/object-graph.json
RIBOS_R18_AMD64_SRCS := $(RIBOS_R18_COMMON_SRCS) \
	src/arch/x86_64/arch.c \
	src/arch/x86_64/io.c \
	ports/qemu/pc-x86_64/port.c \
	targets/ribos-validation/x86_64-uefi-entry.c
RIBOS_R18_AMD64_OBJS := \
	$(RIBOS_R18_AMD64_SRCS:%.c=$(RIBOS_R18_AMD64_DIR)/obj/%.o) \
	$(RIBOS_R18_AMD64_DIR)/obj/generated/plugin_registry.o \
	$(RIBOS_R18_AMD64_DIR)/obj/generated/embedded_policy.o \
	$(RIBOS_R18_AMD64_DIR)/obj/generated/embedded_trial_policy.o
RIBOS_R18_AMD64_VM_OBJS := \
	$(RIBOS_TARGET_CORE_SRCS:%.c=$(RIBOS_R18_AMD64_DIR)/obj/%.o)
RIBOS_R18_AMD64_VM_LIB := $(RIBOS_R18_AMD64_DIR)/libribos-target-core.a
RIBOS_R18_AMD64_APP := $(RIBOS_R18_AMD64_DIR)/BOOTX64.EFI
RIBOS_R18_AMD64_ESP := $(RIBOS_R18_AMD64_DIR)/esp

BIOS_DIR := $(TARGET_BUILD_ROOT)/x86-bios-client
BIOS_MANIFEST := products/bootmgr/manifests/x86-bios-parus.json
BIOS_REGISTRY_C := $(BIOS_DIR)/generated/plugin_registry.c
BIOS_GRAPH := $(BIOS_DIR)/results/object-graph.json
BIOS_OBJECTS := \
	$(BIOS_DIR)/obj/src/environments/bios-client/bios_client.o \
	$(BIOS_DIR)/obj/targets/x86-bios-client/compile_probe.o

FIRMWARE_PROVIDER_ROOT := $(BUILD_ROOT)/firmware-providers
UEFI_PROVIDER_DIR := $(FIRMWARE_PROVIDER_ROOT)/uefi-compatible-reference
UEFI_PROVIDER_MANIFEST := products/firmware/manifests/uefi-compatible-reference.json
UEFI_PROVIDER_REGISTRY_C := $(UEFI_PROVIDER_DIR)/generated/plugin_registry.c
UEFI_PROVIDER_REPORT := $(UEFI_PROVIDER_DIR)/results/object-graph.json
UEFI_PROVIDER_BIN := $(UEFI_PROVIDER_DIR)/ribon-firmware-provider-uefi-reference
UEFI_PROVIDER_SRCS := \
	src/arch/common.c \
	src/arch/x86_64/arch.c \
	src/firmware/uefi-compatible/personality.c \
	products/firmware/uefi-compatible-reference/main.c
UEFI_PROVIDER_OBJS := $(UEFI_PROVIDER_SRCS:%.c=$(UEFI_PROVIDER_DIR)/obj/%.o)
UEFI_PROVIDER_OBJS += $(UEFI_PROVIDER_DIR)/obj/generated/plugin_registry.o

BIOS_PROVIDER_DIR := $(FIRMWARE_PROVIDER_ROOT)/bios-compatible-reference
BIOS_PROVIDER_MANIFEST := products/firmware/manifests/bios-compatible-reference.json
BIOS_PROVIDER_REGISTRY_C := $(BIOS_PROVIDER_DIR)/generated/plugin_registry.c
BIOS_PROVIDER_REPORT := $(BIOS_PROVIDER_DIR)/results/object-graph.json
BIOS_PROVIDER_BIN := $(BIOS_PROVIDER_DIR)/ribon-firmware-provider-bios-reference
BIOS_PROVIDER_SRCS := \
	src/arch/common.c \
	src/arch/x86_64/arch.c \
	src/firmware/bios-compatible/personality.c \
	products/firmware/bios-compatible-reference/main.c
BIOS_PROVIDER_OBJS := $(BIOS_PROVIDER_SRCS:%.c=$(BIOS_PROVIDER_DIR)/obj/%.o)
BIOS_PROVIDER_OBJS += $(BIOS_PROVIDER_DIR)/obj/generated/plugin_registry.o
