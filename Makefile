ROOT := $(abspath .)
RIBON_ARCH ?= x86_64
RIBON_ARCHES := x86_64 aarch64 riscv64
BUILD_ROOT ?= $(ROOT)/build
BUILD_DIR ?= $(BUILD_ROOT)/$(RIBON_ARCH)
CC ?= cc
AR ?= ar
PYTHON ?= python3
QSTAR ?= qstar
DOXYGEN ?= doxygen
SPHINX_BUILD ?= $(firstword $(wildcard $(BUILD_ROOT)/docs/venv/bin/sphinx-build) sphinx-build)
AARCH64_CC ?= /usr/bin/clang
RISCV64_CC ?= /opt/homebrew/opt/llvm@20/bin/clang
LD_LLD ?= /opt/homebrew/bin/ld.lld
LLD_LINK ?= /opt/homebrew/bin/lld-link
OBJCOPY ?= /opt/homebrew/opt/llvm@20/bin/llvm-objcopy
LLVM_AR ?= /opt/homebrew/opt/llvm@20/bin/llvm-ar
QEMU_AARCH64 ?= /opt/homebrew/bin/qemu-system-aarch64
QEMU_X86_64 ?= /opt/homebrew/bin/qemu-system-x86_64
QEMU_RISCV64 ?= /opt/homebrew/bin/qemu-system-riscv64
RISCV64_OPENSBI_FIRMWARE ?= /opt/homebrew/Cellar/qemu/11.0.2/share/qemu/opensbi-riscv64-generic-fw_dynamic.bin
X86_64_UEFI_FIRMWARE ?= /opt/homebrew/Cellar/qemu/11.0.2/share/qemu/edk2-x86_64-code.fd

# BSD ar records member timestamps. LLVM ar defaults to deterministic archives,
# which keeps the installed SDK and the host tools linked from it path/time stable.
ifeq ($(shell uname -s),Darwin)
AR := $(LLVM_AR)
endif

CFLAGS ?= -std=c11 -O2 -g
CFLAGS += -ffile-prefix-map=$(BUILD_ROOT)=build \
	-ffile-prefix-map=$(ROOT)=. \
	-fdebug-prefix-map=$(BUILD_ROOT)=build \
	-fdebug-prefix-map=$(ROOT)=.
WARNFLAGS := -Wall -Wextra -Werror
DEPFLAGS := -MMD -MP
CPPFLAGS += -I$(ROOT)/include
FREESTANDING_FLAGS := -std=c11 -O2 -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables \
	-Wall -Wextra -Werror -I$(ROOT)/include/freestanding -I$(ROOT)/include
AARCH64_FLAGS := --target=aarch64-none-elf $(FREESTANDING_FLAGS) \
	-mgeneral-regs-only -mstrict-align
RISCV64_FLAGS := --target=riscv64-none-elf $(FREESTANDING_FLAGS) \
	-march=rv64gc -mabi=lp64d -mcmodel=medany
UEFI_FLAGS := --target=x86_64-pc-win32-coff $(FREESTANDING_FLAGS) \
	-fshort-wchar -mno-red-zone -I$(ROOT)/include/uefi \
	-I$(ROOT)/include/uefi/X64
BIOS_FLAGS := --target=i386-none-elf $(FREESTANDING_FLAGS) -m32

# Parus-specific product smokes require the complete kernel-owned boot chain.
# Generic target smokes above/below retain protocol-neutral Ribon markers only.
PARUS_SUCCESS_MARKER_ARGS := \
	--required-marker PARUS:BM:v0:01000100:LOCORE:ENTER:NONE \
	--required-marker PARUS:BM:v0:02000200:STAGE0:OK:NONE \
	--required-marker PARUS:BM:v0:03000200:XIBALBA:OK:NONE \
	--required-marker PARUS:BM:v0:04000200:EB0:OK:NONE \
	--required-marker PARUS:BM:v0:04010200:EB1:OK:NONE \
	--required-marker PARUS:BM:v0:04020200:EB2:OK:NONE \
	--required-marker PARUS:BM:v0:04030200:EB3:OK:NONE \
	--required-marker PARUS:BM:v0:04040200:EB4:OK:NONE \
	--required-marker PARUS:BM:v0:04050200:EB5:OK:NONE \
	--required-marker PARUS:BM:v0:04060200:EB6:OK:NONE \
	--required-marker PARUS:BM:v0:04070200:EB7:OK:NONE \
	--required-marker PARUS:BM:v0:04080200:EB8:OK:NONE \
	--required-marker PARUS:BM:v0:04090200:EB9:OK:NONE \
	--required-marker PARUS:BM:v0:05000100:KMAIN:ENTER:NONE \
	--required-marker PARUS:BM:v0:06000200:IDLE:OK:NONE

ifeq ($(filter $(RIBON_ARCH),$(RIBON_ARCHES)),)
$(error unsupported RIBON_ARCH=$(RIBON_ARCH); supported: $(RIBON_ARCHES))
endif

ifeq ($(RIBON_ARCH),x86_64)
HOST_PLATFORM_DEFINES := \
	-DRIBON_HOST_PLATFORM_ARCH=RIBON_ARCHITECTURE_X86_64 \
	-DRIBON_HOST_PLATFORM_ARCH_MASK=RIBON_ARCH_MASK_X86_64
else ifeq ($(RIBON_ARCH),aarch64)
HOST_PLATFORM_DEFINES := \
	-DRIBON_HOST_PLATFORM_ARCH=RIBON_ARCHITECTURE_AARCH64 \
	-DRIBON_HOST_PLATFORM_ARCH_MASK=RIBON_ARCH_MASK_AARCH64
else
HOST_PLATFORM_DEFINES := \
	-DRIBON_HOST_PLATFORM_ARCH=RIBON_ARCHITECTURE_RISCV64 \
	-DRIBON_HOST_PLATFORM_ARCH_MASK=RIBON_ARCH_MASK_RISCV64
endif

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
QEMU_RISCV64_ELF := $(QEMU_RISCV64_DIR)/ribon.elf
QEMU_RISCV64_IMAGE := $(QEMU_RISCV64_DIR)/ribon.bin
QEMU_RISCV64_VALIDATION := $(QEMU_RISCV64_DIR)/results/external-payload.json
QEMU_RISCV64_SRCS := $(RAW_COMMON_SRCS) \
	src/arch/riscv64/arch.c \
	ports/qemu/virt-riscv64/port.c
QEMU_RISCV64_OBJS := $(QEMU_RISCV64_SRCS:%.c=$(QEMU_RISCV64_DIR)/obj/%.o)
QEMU_RISCV64_OBJS += \
	$(QEMU_RISCV64_DIR)/obj/generated/plugin_registry.o \
	$(QEMU_RISCV64_DIR)/obj/generated/embedded_payload.o \
	$(QEMU_RISCV64_DIR)/obj/targets/qemu-riscv64-virt-opensbi/entry.o

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

.PHONY: all lib sdk-install host-reference check check-one check-loader check-pe-coff \
	check-fdt check-rph1 check-arch-x86_64 check-arch-aarch64 \
	check-arch-ops check-core-service check-port-services check-boot-lifecycle \
	check-environment-persistent-inputs check-boot-modules check-media-pipeline \
	check-mode-descriptors check-plugin-descriptors check-protocol-contract \
	check-parus-entry-contract \
	check-os-packages \
	check-linux-boot \
	check-linux-external-input \
	check-library-embed check-object-graphs check-public-api \
	check-composition-schemas check-sdk-surface check-sdk-embed \
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
	qemu-riscv64-virt-rph1-fixture-product \
	qemu-riscv64-virt-rph1-fixture-smoke \
	x86_64-uefi-parus-external x86_64-uefi-parus-external-product \
	x86_64-uefi-parus-fixture-smoke x86_64-uefi-parus-external-smoke \
	x86_64-uefi-linux-product x86_64-uefi-linux-smoke \
	check-terminal-image-launch check-uefi-managed-image check-linux-x86_64-efi \
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
$(RIBOS_OBJECT_DIR)/target/$(1).o: $(2) $(RIBOS_HEADERS) Makefile
	@mkdir -p $$(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) -ffreestanding -fno-builtin \
		$(RIBOS_TARGET_INCLUDE_FLAGS) -c $$< -o $$@
endef

define RIBOS_HOST_SUPPORT_OBJECT
$(RIBOS_OBJECT_DIR)/host-support/$(1).o: $(2) $(RIBOS_HEADERS) Makefile
	@mkdir -p $$(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) -c $$< -o $$@
endef

define RIBOS_HOST_COMPILER_OBJECT
$(RIBOS_OBJECT_DIR)/host-compiler/$(1).o: $(2) $(RIBOS_HEADERS) Makefile
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

$(RIBOS_POLICY_OBJ): src/plugins/policy/ribos/adapter.c \
		include/Ribon/policy/ribos.h include/Ribon/security/key_policy.h \
		include/Ribon/security/protected_state.h $(RIBOS_HEADERS) Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(DEPFLAGS) \
		$(RIBOS_TARGET_INCLUDE_FLAGS) -c $< -o $@

$(HOST_SECURITY_OBJS): CPPFLAGS += $(SECURITY_INCLUDE_FLAGS)
$(SDK_UPDATE_OBJS): CPPFLAGS += $(SECURITY_INCLUDE_FLAGS)

$(BUILD_DIR)/obj/src/environments/host/ribos_policy.o: \
		src/environments/host/ribos_policy.c \
		src/environments/host/ribos_policy.h $(RIBOS_HEADERS) Makefile
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

$(RIBOS_TARGET_CORE_LIB): $(RIBOS_TARGET_CORE_OBJS) Makefile
	@mkdir -p $(@D)
	$(RM) $@
	$(AR) rcs $@ $(RIBOS_TARGET_CORE_OBJS)

$(RIBOS_POLICY_LIB): $(RIBOS_POLICY_OBJ) Makefile
	@mkdir -p $(@D)
	$(RM) $@
	$(AR) rcs $@ $(RIBOS_POLICY_OBJ)

$(RIBOS_HOST_SUPPORT_LIB): $(RIBOS_HOST_SUPPORT_OBJS) Makefile
	@mkdir -p $(@D)
	$(RM) $@
	$(AR) rcs $@ $(RIBOS_HOST_SUPPORT_OBJS)

$(RIBOS_HOST_COMPILER_LIB): $(RIBOS_HOST_COMPILER_OBJS) Makefile
	@mkdir -p $(@D)
	$(RM) $@
	$(AR) rcs $@ $(RIBOS_HOST_COMPILER_OBJS)

ribos-libraries: $(RIBOS_TARGET_CORE_LIB) $(RIBOS_HOST_SUPPORT_LIB) \
		$(RIBOS_HOST_COMPILER_LIB)

$(RIBOS_OBJECT_DIR)/tools/parse.o: language/ribos/host/tools/parse.c \
		$(RIBOS_HEADERS) Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) -c $< -o $@

$(RIBOS_PARSER_PILOT): $(RIBOS_OBJECT_DIR)/tools/parse.o \
		$(RIBOS_HOST_COMPILER_LIB) $(RIBOS_TARGET_CORE_LIB) \
		$(RIBOS_HOST_SUPPORT_LIB) Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) \
		$(RIBOS_OBJECT_DIR)/tools/parse.o \
		$(RIBOS_HOST_COMPILER_LIB) $(RIBOS_TARGET_CORE_LIB) \
		$(RIBOS_HOST_SUPPORT_LIB) -o $@

ribosc: $(RIBOS_PARSER_PILOT)

ribos-parser-pilot: $(RIBOS_PARSER_PILOT)

check-ribos-parser-snapshot:
	$(PYTHON) language/ribos/host/pegen/check_parser_snapshot.py

check-ribos-parser-pilot: check-ribos-parser-snapshot $(RIBOS_PARSER_PILOT)
	$(PYTHON) language/ribos/frontend/tests/parser_pilot_tests.py \
		--parser $(RIBOS_PARSER_PILOT)
	$(RIBOS_PARSER_PILOT) \
		language/ribos/examples/executable/minimal_recovery.rbs

check-ribos-semantics: check-ribos-parser-snapshot $(RIBOS_PARSER_PILOT)
	$(PYTHON) language/ribos/frontend/tests/semantic_tests.py \
		--parser $(RIBOS_PARSER_PILOT)

$(RIBOS_SCHEMA_TEST): language/ribos/schema/tests/schema_tests.c \
		$(RIBOS_TARGET_CORE_LIB) Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) \
		language/ribos/schema/tests/schema_tests.c \
		$(RIBOS_TARGET_CORE_LIB) -o $@

check-ribos-schema: $(RIBOS_SCHEMA_TEST)
	$(RIBOS_SCHEMA_TEST)

$(RIBOS_IR_MODULE_TEST): language/ribos/ir/tests/module_tests.c \
		$(RIBOS_HOST_COMPILER_LIB) $(RIBOS_TARGET_CORE_LIB) \
		$(RIBOS_HOST_SUPPORT_LIB) Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) \
		language/ribos/ir/tests/module_tests.c \
		$(RIBOS_HOST_COMPILER_LIB) $(RIBOS_TARGET_CORE_LIB) \
		$(RIBOS_HOST_SUPPORT_LIB) -o $@

$(RIBOS_IR_RESOURCE_TEST): language/ribos/ir/tests/resource_tests.c \
		$(RIBOS_HOST_COMPILER_LIB) $(RIBOS_TARGET_CORE_LIB) \
		$(RIBOS_HOST_SUPPORT_LIB) Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) \
		language/ribos/ir/tests/resource_tests.c \
		$(RIBOS_HOST_COMPILER_LIB) $(RIBOS_TARGET_CORE_LIB) \
		$(RIBOS_HOST_SUPPORT_LIB) -o $@

check-ribos-ir: check-ribos-parser-snapshot $(RIBOS_PARSER_PILOT) \
		$(RIBOS_IR_MODULE_TEST)
	$(RIBOS_IR_MODULE_TEST)
	$(PYTHON) language/ribos/ir/tests/ir_tests.py \
		--compiler $(RIBOS_PARSER_PILOT)

check-ribos-resources: check-ribos-parser-snapshot \
		$(RIBOS_PARSER_PILOT) $(RIBOS_IR_RESOURCE_TEST)
	$(RIBOS_IR_RESOURCE_TEST)
	$(PYTHON) language/ribos/ir/tests/resource_tests.py \
		--compiler $(RIBOS_PARSER_PILOT)

$(RIBOS_ARTIFACT_TEST): language/ribos/artifact/tests/artifact_tests.c \
		$(RIBOS_HOST_COMPILER_LIB) $(RIBOS_TARGET_CORE_LIB) \
		$(RIBOS_HOST_SUPPORT_LIB) Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) \
		language/ribos/artifact/tests/artifact_tests.c \
		$(RIBOS_HOST_COMPILER_LIB) $(RIBOS_TARGET_CORE_LIB) \
		$(RIBOS_HOST_SUPPORT_LIB) -o $@

check-ribos-artifact: check-ribos-parser-snapshot \
		$(RIBOS_PARSER_PILOT) $(RIBOS_ARTIFACT_TEST) \
		tools/inspect_ribos_trust_message.py \
		tests/fixtures/security/ribos-policy-trust-v1.json
	$(RIBOS_ARTIFACT_TEST)
	$(PYTHON) language/ribos/artifact/tests/artifact_tests.py \
		--compiler $(RIBOS_PARSER_PILOT)
	$(PYTHON) language/ribos/artifact/tests/trust_message_tests.py \
		--c-codec $(RIBOS_ARTIFACT_TEST) \
		--inspector tools/inspect_ribos_trust_message.py \
		--vector tests/fixtures/security/ribos-policy-trust-v1.json

$(RIBOS_VERIFIER): language/ribos/host/tools/verify.c \
		$(RIBOS_TARGET_CORE_LIB) Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) \
		language/ribos/host/tools/verify.c \
		$(RIBOS_TARGET_CORE_LIB) -o $@

ribos-verify: $(RIBOS_VERIFIER)

$(RIBOS_OBJECT_DIR)/tools/run.o: language/ribos/host/tools/run.c \
		$(RIBOS_HEADERS) Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) -c $< -o $@

$(RIBOS_RUNNER): $(RIBOS_OBJECT_DIR)/tools/run.o \
		$(RIBOS_TARGET_CORE_LIB) Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) \
		$(RIBOS_OBJECT_DIR)/tools/run.o \
		$(RIBOS_TARGET_CORE_LIB) -o $@

ribos-run: $(RIBOS_RUNNER)

check-ribos-host-boundary: ribos-libraries $(RIBOS_ALLOCATOR_TEST)
	$(PYTHON) language/ribos/host/tests/check_boundary.py \
		--target-archive $(RIBOS_TARGET_CORE_LIB) \
		--host-compiler-archive $(RIBOS_HOST_COMPILER_LIB)
	$(RIBOS_ALLOCATOR_TEST)

$(RIBOS_ALLOCATOR_TEST): language/ribos/host/tests/allocator_tests.c \
		$(RIBOS_HOST_COMPILER_LIB) $(RIBOS_TARGET_CORE_LIB) \
		$(RIBOS_HOST_SUPPORT_LIB) Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) \
		language/ribos/host/tests/allocator_tests.c \
		$(RIBOS_HOST_COMPILER_LIB) $(RIBOS_TARGET_CORE_LIB) \
		$(RIBOS_HOST_SUPPORT_LIB) -o $@

check-ribos-verifier: check-ribos-artifact $(RIBOS_VERIFIER)
	$(PYTHON) language/ribos/vm/tests/verifier_tests.py \
		--compiler $(RIBOS_PARSER_PILOT) \
		--verifier $(RIBOS_VERIFIER)

$(RIBOS_RUNTIME_CONTRACT_TEST): \
		language/ribos/vm/tests/runtime_contract_tests.c \
		language/ribos/vm/include/ribos/vm/runtime.h \
		$(RIBOS_TARGET_CORE_LIB) Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) \
		language/ribos/vm/tests/runtime_contract_tests.c \
		$(RIBOS_TARGET_CORE_LIB) -o $@

check-ribos-runtime-contract: $(RIBOS_RUNTIME_CONTRACT_TEST)
	$(PYTHON) language/ribos/vm/tests/check_runtime_header.py
	$(RIBOS_RUNTIME_CONTRACT_TEST)

$(RIBOS_PREPARED_PROGRAM_TEST): \
		language/ribos/vm/tests/prepared_program_tests.c \
		$(RIBOS_TARGET_CORE_LIB) Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) \
		language/ribos/vm/tests/prepared_program_tests.c \
		$(RIBOS_TARGET_CORE_LIB) -o $@

check-ribos-prepared-program: check-ribos-verifier \
		$(RIBOS_PREPARED_PROGRAM_TEST)
	$(PYTHON) language/ribos/vm/tests/check_no_raw_execute.py
	@tmp=$$(mktemp -d); \
		trap 'rm -rf "$$tmp"' EXIT; \
		$(RIBOS_PARSER_PILOT) --emit-artifact \
			"$$tmp/policy.rba" \
			language/ribos/frontend/tests/semantic/positive/result_match.rbs; \
		$(RIBOS_PREPARED_PROGRAM_TEST) "$$tmp/policy.rba"

$(RIBOS_RUNTIME_STORAGE_TEST): \
		language/ribos/vm/tests/runtime_storage_tests.c \
		$(RIBOS_TARGET_CORE_LIB) Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) \
		language/ribos/vm/tests/runtime_storage_tests.c \
		$(RIBOS_TARGET_CORE_LIB) -o $@

check-ribos-runtime-storage: check-ribos-prepared-program \
		$(RIBOS_RUNTIME_STORAGE_TEST)
	@tmp=$$(mktemp -d); \
		trap 'rm -rf "$$tmp"' EXIT; \
		$(RIBOS_PARSER_PILOT) --emit-artifact \
			"$$tmp/runtime-storage.rba" \
			language/ribos/vm/tests/runtime_storage.rbs; \
		$(RIBOS_RUNTIME_STORAGE_TEST) "$$tmp/runtime-storage.rba"

$(RIBOS_VM_SCALAR_TEST): \
		language/ribos/vm/tests/scalar_interpreter_tests.c \
		$(RIBOS_TARGET_CORE_LIB) Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) \
		-Ilanguage/ribos/vm/src/runtime \
		language/ribos/vm/tests/scalar_interpreter_tests.c \
		$(RIBOS_TARGET_CORE_LIB) -o $@

check-ribos-vm-scalar: check-ribos-runtime-storage \
		$(RIBOS_VM_SCALAR_TEST)
	$(PYTHON) language/ribos/vm/tests/check_interpreter_boundary.py
	@tmp=$$(mktemp -d); \
		trap 'rm -rf "$$tmp"' EXIT; \
		$(RIBOS_PARSER_PILOT) --emit-artifact \
			"$$tmp/scalar-interpreter.rba" \
			language/ribos/vm/tests/scalar_interpreter.rbs; \
		$(RIBOS_VM_SCALAR_TEST) "$$tmp/scalar-interpreter.rba"

$(RIBOS_VM_CALLS_LOOPS_TEST): \
		language/ribos/vm/tests/calls_loops_interpreter_tests.c \
		$(RIBOS_TARGET_CORE_LIB) Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) \
		-Ilanguage/ribos/vm/src/runtime \
		language/ribos/vm/tests/calls_loops_interpreter_tests.c \
		$(RIBOS_TARGET_CORE_LIB) -o $@

check-ribos-vm-calls: check-ribos-vm-scalar \
		$(RIBOS_VM_CALLS_LOOPS_TEST)
	@tmp=$$(mktemp -d); \
		trap 'rm -rf "$$tmp"' EXIT; \
		$(RIBOS_PARSER_PILOT) --emit-artifact \
			"$$tmp/calls-loops-interpreter.rba" \
			language/ribos/vm/tests/calls_loops_interpreter.rbs; \
		$(RIBOS_VM_CALLS_LOOPS_TEST) \
			"$$tmp/calls-loops-interpreter.rba" calls

check-ribos-vm-loops: check-ribos-vm-scalar \
		$(RIBOS_VM_CALLS_LOOPS_TEST)
	@tmp=$$(mktemp -d); \
		trap 'rm -rf "$$tmp"' EXIT; \
		$(RIBOS_PARSER_PILOT) --emit-artifact \
			"$$tmp/calls-loops-interpreter.rba" \
			language/ribos/vm/tests/calls_loops_interpreter.rbs; \
		$(RIBOS_VM_CALLS_LOOPS_TEST) \
			"$$tmp/calls-loops-interpreter.rba" loops

check-ribos-vm-aggregates: check-ribos-vm-calls \
		$(RIBOS_VM_CALLS_LOOPS_TEST)
	@tmp=$$(mktemp -d); \
		trap 'rm -rf "$$tmp"' EXIT; \
		$(RIBOS_PARSER_PILOT) --emit-artifact \
			"$$tmp/aggregate-interpreter.rba" \
			language/ribos/vm/tests/aggregate_interpreter.rbs; \
		$(RIBOS_VM_CALLS_LOOPS_TEST) \
			"$$tmp/aggregate-interpreter.rba" aggregates; \
		$(RIBOS_PARSER_PILOT) --emit-artifact \
			"$$tmp/aggregate-calls.rba" \
			language/ribos/frontend/tests/semantic/positive/aggregate_lowering.rbs; \
		$(RIBOS_VM_CALLS_LOOPS_TEST) \
			"$$tmp/aggregate-calls.rba" aggregate-calls

$(RIBOS_VM_HANDLES_TEST): \
		language/ribos/vm/tests/handle_runtime_tests.c \
		$(RIBOS_TARGET_CORE_LIB) Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) \
		language/ribos/vm/tests/handle_runtime_tests.c \
		$(RIBOS_TARGET_CORE_LIB) -o $@

check-ribos-vm-handles: check-ribos-vm-aggregates \
		$(RIBOS_VM_HANDLES_TEST)
	@tmp=$$(mktemp -d); \
		trap 'rm -rf "$$tmp"' EXIT; \
		$(RIBOS_PARSER_PILOT) --emit-artifact \
			"$$tmp/handle-runtime.rba" \
			language/ribos/frontend/tests/semantic/positive/result_match.rbs; \
		$(RIBOS_PARSER_PILOT) --emit-artifact \
			"$$tmp/aggregate-ownership.rba" \
			language/ribos/vm/tests/aggregate_ownership.rbs; \
		$(RIBOS_VM_HANDLES_TEST) \
			"$$tmp/handle-runtime.rba" \
			"$$tmp/aggregate-ownership.rba"

check-ribos-vm-helpers: check-ribos-vm-handles
	@echo "RIBOS-VM-HELPERS-OK dispatch=stable-id signature=typed \
budgets=bounded handles=generation-checked evidence=host-fake"

$(RIBOS_VM_TERMINAL_TEST): \
		language/ribos/vm/tests/terminal_runtime_tests.c \
		$(RIBOS_TARGET_CORE_LIB) Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) \
		-Ilanguage/ribos/vm/src/runtime \
		language/ribos/vm/tests/terminal_runtime_tests.c \
		$(RIBOS_TARGET_CORE_LIB) -o $@

check-ribos-vm-terminal: check-ribos-vm-helpers \
		$(RIBOS_VM_TERMINAL_TEST)
	@tmp=$$(mktemp -d); \
		trap 'rm -rf "$$tmp"' EXIT; \
		$(RIBOS_PARSER_PILOT) --emit-artifact \
			"$$tmp/action.rba" \
			language/ribos/vm/tests/runtime_storage.rbs; \
		$(RIBOS_PARSER_PILOT) --emit-artifact \
			"$$tmp/error.rba" \
			language/ribos/vm/tests/terminal_policy_error.rbs; \
		$(RIBOS_PARSER_PILOT) --emit-artifact \
			"$$tmp/journal.rba" \
			language/ribos/vm/tests/terminal_journal.rbs; \
		$(RIBOS_VM_TERMINAL_TEST) \
			"$$tmp/action.rba" "$$tmp/error.rba" \
			"$$tmp/journal.rba"

check-ribos-vm-faults: check-ribos-vm-terminal
	@echo "RIBOS-VM-FAULT-CLOSURE-OK receipt=fixed-size \
recovery=once authority=revoked rollback-claim=none evidence=host-unit"

check-ribos-host-tools: check-ribos-parser-snapshot \
		$(RIBOS_PARSER_PILOT) $(RIBOS_VERIFIER) $(RIBOS_RUNNER)
	$(RIBOS_PARSER_PILOT) --help
	$(RIBOS_VERIFIER) --help
	$(RIBOS_RUNNER) --help
	@echo "RIBOS-HOST-TOOLS-OK compiler=ribosc verifier=ribos-verify \
runner=ribos-run vm-core=shared evidence=host-build"

check-ribos-replay: check-ribos-vm-terminal check-ribos-host-tools
	$(PYTHON) language/ribos/host/tests/replay_tests.py \
		--compiler $(RIBOS_PARSER_PILOT) \
		--verifier $(RIBOS_VERIFIER) \
		--runner $(RIBOS_RUNNER)

check-ribos-conformance: check-ribos-replay
	$(PYTHON) language/ribos/host/tests/conformance_tests.py \
		--compiler $(RIBOS_PARSER_PILOT) \
		--verifier $(RIBOS_VERIFIER) \
		--runner $(RIBOS_RUNNER)

check-ribos-hostile: check-ribos-conformance
	$(PYTHON) language/ribos/host/tests/hostile_tests.py \
		--compiler $(RIBOS_PARSER_PILOT) \
		--verifier $(RIBOS_VERIFIER) \
		--runner $(RIBOS_RUNNER)

check-ribos-executable-corpus: check-ribos-parser-pilot \
		check-ribos-semantics check-ribos-vm-terminal check-ribos-host-tools
	$(PYTHON) language/ribos/examples/tests/executable_corpus_tests.py \
		--compiler $(RIBOS_PARSER_PILOT) \
		--verifier $(RIBOS_VERIFIER) \
		--runner $(RIBOS_RUNNER)

check-ribos-vm: check-ribos-vm-faults check-ribos-hostile \
		check-ribos-executable-corpus
	@echo "RIBOS-VM-R16-AGGREGATE-OK core=production \
replay=deterministic conformance=24-opcodes hostile=bounded \
executable-examples=6 evidence=host-only"

# Generation is intentionally explicit. Normal builds compile and validate the
# tracked snapshot without importing or invoking Pegen.
ribos-parser-generate:
	@test -n "$(RIBOS_PEGEN_ROOT)" || \
		{ echo "RIBOS_PEGEN_ROOT must name the pinned CPython Pegen root"; exit 2; }
	$(PYTHON) language/ribos/host/pegen/generate_parser.py \
		--pegen-root $(RIBOS_PEGEN_ROOT)

ribos-parser-regenerate-check:
	@test -n "$(RIBOS_PEGEN_ROOT)" || \
		{ echo "RIBOS_PEGEN_ROOT must name the pinned CPython Pegen root"; exit 2; }
	$(PYTHON) language/ribos/host/pegen/generate_parser.py \
		--pegen-root $(RIBOS_PEGEN_ROOT) --check

$(BUILD_DIR)/obj/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(DEPFLAGS) -c $< -o $@

$(TEST_BUILD_DIR)/obj/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(DEPFLAGS) -c $< -o $@

$(GENERATED_REGISTRY_C): $(HOST_MANIFEST) tools/generate_plugin_registry.py
	$(PYTHON) tools/generate_plugin_registry.py \
		--manifest $(HOST_MANIFEST) \
		--architecture $(RIBON_ARCH) \
		--output $@ \
		--report $(GENERATED_REGISTRY_REPORT)

$(GENERATED_REGISTRY_O): $(GENERATED_REGISTRY_C)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(DEPFLAGS) \
		$(RIBOS_TARGET_INCLUDE_FLAGS) -c $< -o $@

$(CORE_LIB): $(CORE_OBJS) Makefile
	@mkdir -p $(@D)
	$(RM) $@
	$(AR) rcs $@ $(CORE_OBJS)

$(BOOT_LIB): $(BOOT_LIB_OBJS) Makefile
	@mkdir -p $(@D)
	$(RM) $@
	$(AR) rcs $@ $(BOOT_LIB_OBJS)

$(SDK_LIB): $(SDK_LIB_OBJS) Makefile
	@mkdir -p $(@D)
	$(RM) $@
	$(AR) rcs $@ $(SDK_LIB_OBJS)

$(SDK_UPDATE_LIB): $(SDK_UPDATE_OBJS) Makefile
	@mkdir -p $(@D)
	$(RM) $@
	$(AR) rcs $@ $(SDK_UPDATE_OBJS)

$(HOST_REFERENCE): \
	$(HOST_MAIN_OBJ) $(ARCH_OBJS) $(HOST_PRODUCT_OBJS) \
	$(GENERATED_REGISTRY_O) $(RIBOS_POLICY_LIB) \
	$(BOOT_LIB) $(CORE_LIB) $(RIBOS_TARGET_CORE_LIB) $(HOST_SECURITY_OBJS)
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(RIBOS_RIBON_INTEGRATION_TEST): \
		tests/policy/ribos_integration_tests.c \
		$(ARCH_OBJS) $(HOST_PRODUCT_OBJS) $(GENERATED_REGISTRY_O) \
		$(RIBOS_POLICY_LIB) $(BOOT_LIB) $(CORE_LIB) \
		$(RIBOS_TARGET_CORE_LIB) $(HOST_SECURITY_OBJS) Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) \
		$(RIBOS_TARGET_INCLUDE_FLAGS) \
		tests/policy/ribos_integration_tests.c \
		$(ARCH_OBJS) $(HOST_PRODUCT_OBJS) $(GENERATED_REGISTRY_O) \
		$(RIBOS_POLICY_LIB) $(BOOT_LIB) $(CORE_LIB) \
		$(RIBOS_TARGET_CORE_LIB) $(HOST_SECURITY_OBJS) -o $@

$(RIBOS_HOST_UNSIGNED): $(RIBOS_PARSER_PILOT) \
		language/ribos/vm/tests/aggregate_ownership.rbs
	@mkdir -p $(@D)
	$(RIBOS_PARSER_PILOT) --emit-artifact $@ \
		language/ribos/vm/tests/aggregate_ownership.rbs

$(RIBOS_HOST_SIGNED_A): $(RIBOS_HOST_UNSIGNED) $(HOST_MANIFEST) \
		tests/fixtures/security/rfc8032-test1-seed.hex tools/sign_ribos_policy.py
	$(PYTHON) tools/sign_ribos_policy.py --input $< --output $@ \
		--product-manifest $(HOST_MANIFEST) \
		--private-seed tests/fixtures/security/rfc8032-test1-seed.hex \
		--key-id ribon-host-reference-key \
		--rollback-domain ribon.policy.host-reference.v1 \
		--sequence 1 --mode normal \
		--expected-public-key \
			d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a

$(RIBOS_HOST_SIGNED_B): $(RIBOS_HOST_UNSIGNED) $(HOST_MANIFEST) \
		tests/fixtures/security/rfc8032-test1-seed.hex tools/sign_ribos_policy.py
	$(PYTHON) tools/sign_ribos_policy.py --input $< --output $@ \
		--product-manifest $(HOST_MANIFEST) \
		--private-seed tests/fixtures/security/rfc8032-test1-seed.hex \
		--key-id ribon-host-reference-key \
		--rollback-domain ribon.policy.host-reference.v1 \
		--sequence 2 --mode normal \
		--expected-public-key \
			d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a

$(RIBOS_HOST_WRONG_KEY): $(RIBOS_HOST_UNSIGNED) $(HOST_MANIFEST) \
		tests/fixtures/security/rfc8032-test1-seed.hex tools/sign_ribos_policy.py
	$(PYTHON) tools/sign_ribos_policy.py --input $< --output $@ \
		--product-manifest $(HOST_MANIFEST) \
		--private-seed tests/fixtures/security/rfc8032-test1-seed.hex \
		--key-id ribon-host-unknown-key \
		--rollback-domain ribon.policy.host-reference.v1 \
		--sequence 1 --mode normal \
		--expected-public-key \
			d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a

$(RIBOS_HOST_VERIFIER_INVALID_UNSIGNED): $(RIBOS_HOST_UNSIGNED) \
		tools/make_ribos_verifier_invalid.py
	$(PYTHON) tools/make_ribos_verifier_invalid.py --input $< --output $@

$(RIBOS_HOST_VERIFIER_INVALID): $(RIBOS_HOST_VERIFIER_INVALID_UNSIGNED) \
		$(HOST_MANIFEST) tests/fixtures/security/rfc8032-test1-seed.hex \
		tools/sign_ribos_policy.py
	$(PYTHON) tools/sign_ribos_policy.py --input $< --output $@ \
		--product-manifest $(HOST_MANIFEST) \
		--private-seed tests/fixtures/security/rfc8032-test1-seed.hex \
		--key-id ribon-host-reference-key \
		--rollback-domain ribon.policy.host-reference.v1 \
		--sequence 2 --mode normal \
		--expected-public-key \
			d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a

check-ribos-ribon-integration: $(RIBOS_RIBON_INTEGRATION_TEST) \
		$(RIBOS_HOST_UNSIGNED) $(RIBOS_HOST_SIGNED_A) \
		$(RIBOS_HOST_SIGNED_B) $(RIBOS_HOST_WRONG_KEY) \
		$(RIBOS_HOST_VERIFIER_INVALID)
	$(RIBOS_RIBON_INTEGRATION_TEST) $(RIBOS_HOST_UNSIGNED) \
		$(RIBOS_HOST_SIGNED_A) $(RIBOS_HOST_SIGNED_B) \
		$(RIBOS_HOST_WRONG_KEY) $(RIBOS_HOST_VERIFIER_INVALID)

check-ribos-product-graphs:
	$(PYTHON) tools/lint/ribos_product_graph_lint.py \
		--manifest $(HOST_MANIFEST) --architecture $(RIBON_ARCH)

check-ribos-normal-no-network:
	$(PYTHON) tools/lint/ribos_normal_no_network_lint.py \
		--manifest $(HOST_MANIFEST) --architecture $(RIBON_ARCH)

check-ribos-factory-recovery: check-ribos-ribon-integration
	@echo "RIBOS-FACTORY-RECOVERY-OK external-artifact=optional \
authorization=fail-closed notification=once evidence=host-object"

$(RIBOS_R18_MANIFEST): $(HOST_MANIFEST) tools/make_ribos_qemu_manifest.py
	$(PYTHON) tools/make_ribos_qemu_manifest.py \
		--input $(HOST_MANIFEST) --output $@

$(RIBOS_R18_UNSIGNED_A): $(RIBOS_PARSER_PILOT) \
		language/ribos/vm/tests/aggregate_ownership.rbs
	@mkdir -p $(@D)
	$(RIBOS_PARSER_PILOT) --emit-artifact $@ \
		language/ribos/vm/tests/aggregate_ownership.rbs

$(RIBOS_R18_UNSIGNED_B): $(RIBOS_PARSER_PILOT) \
		language/ribos/vm/tests/aggregate_ownership.rbs
	@mkdir -p $(@D)
	$(RIBOS_PARSER_PILOT) --emit-artifact $@ \
		language/ribos/vm/tests/aggregate_ownership.rbs

$(RIBOS_R18_ARTIFACT): $(RIBOS_R18_UNSIGNED_A) \
		$(RIBOS_R18_MANIFEST) $(RIBOS_R18_GOLDEN_SHA256) \
		tests/fixtures/security/rfc8032-test1-seed.hex \
		tools/sign_ribos_policy.py
	$(PYTHON) tools/sign_ribos_policy.py \
		--input $< --output $@ --product-manifest $(RIBOS_R18_MANIFEST) \
		--private-seed tests/fixtures/security/rfc8032-test1-seed.hex \
		--key-id ribon-validation-policy-key \
		--rollback-domain ribon.policy.ribos-qemu-validation.v1 \
		--sequence 18 --mode normal \
		--expected-public-key \
			d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a \
		--expected-sha256 $(RIBOS_R18_GOLDEN_SHA256)

$(RIBOS_R18_ARTIFACT_B): $(RIBOS_R18_UNSIGNED_B) \
		$(RIBOS_R18_MANIFEST) $(RIBOS_R18_GOLDEN_SHA256) \
		tests/fixtures/security/rfc8032-test1-seed.hex \
		tools/sign_ribos_policy.py
	$(PYTHON) tools/sign_ribos_policy.py \
		--input $< --output $@ --product-manifest $(RIBOS_R18_MANIFEST) \
		--private-seed tests/fixtures/security/rfc8032-test1-seed.hex \
		--key-id ribon-validation-policy-key \
		--rollback-domain ribon.policy.ribos-qemu-validation.v1 \
		--sequence 18 --mode normal \
		--expected-public-key \
			d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a \
		--expected-sha256 $(RIBOS_R18_GOLDEN_SHA256)

$(RIBOS_R18_TRIAL_ARTIFACT): $(RIBOS_R18_UNSIGNED_A) \
		$(RIBOS_R18_MANIFEST) $(RIBOS_R18_TRIAL_GOLDEN_SHA256) \
		tests/fixtures/security/rfc8032-test1-seed.hex \
		tools/sign_ribos_policy.py
	$(PYTHON) tools/sign_ribos_policy.py \
		--input $< --output $@ --product-manifest $(RIBOS_R18_MANIFEST) \
		--private-seed tests/fixtures/security/rfc8032-test1-seed.hex \
		--key-id ribon-validation-policy-key \
		--rollback-domain ribon.policy.ribos-qemu-validation.v1 \
		--sequence 19 --mode normal \
		--expected-public-key \
			d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a \
		--expected-sha256 $(RIBOS_R18_TRIAL_GOLDEN_SHA256)

$(RIBOS_R18_TRIAL_ARTIFACT_B): $(RIBOS_R18_UNSIGNED_B) \
		$(RIBOS_R18_MANIFEST) $(RIBOS_R18_TRIAL_GOLDEN_SHA256) \
		tests/fixtures/security/rfc8032-test1-seed.hex \
		tools/sign_ribos_policy.py
	$(PYTHON) tools/sign_ribos_policy.py \
		--input $< --output $@ --product-manifest $(RIBOS_R18_MANIFEST) \
		--private-seed tests/fixtures/security/rfc8032-test1-seed.hex \
		--key-id ribon-validation-policy-key \
		--rollback-domain ribon.policy.ribos-qemu-validation.v1 \
		--sequence 19 --mode normal \
		--expected-public-key \
			d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a \
		--expected-sha256 $(RIBOS_R18_TRIAL_GOLDEN_SHA256)

$(RIBOS_R18_EMBED_C): $(RIBOS_R18_ARTIFACT) tools/embed_binary.py
	$(PYTHON) tools/embed_binary.py --input $< --output $@ \
		--symbol ribon_ribos_validation_artifact

$(RIBOS_R18_TRIAL_EMBED_C): \
		$(RIBOS_R18_TRIAL_ARTIFACT) tools/embed_binary.py
	$(PYTHON) tools/embed_binary.py --input $< --output $@ \
		--symbol ribon_ribos_validation_trial_artifact

check-ribos-golden-artifact: \
		$(RIBOS_R18_ARTIFACT) $(RIBOS_R18_ARTIFACT_B) \
		$(RIBOS_R18_TRIAL_ARTIFACT) $(RIBOS_R18_TRIAL_ARTIFACT_B)
	cmp $(RIBOS_R18_ARTIFACT) $(RIBOS_R18_ARTIFACT_B)
	cmp $(RIBOS_R18_TRIAL_ARTIFACT) $(RIBOS_R18_TRIAL_ARTIFACT_B)
	@echo "RIBOS-R18-GOLDEN-ARTIFACT-OK policies=2 rebuilds=2 \
wire=little-endian"

$(RIBOS_R18_AARCH64_REGISTRY_C): \
		$(RIBOS_R18_MANIFEST) tools/generate_plugin_registry.py
	$(PYTHON) tools/generate_plugin_registry.py \
		--manifest $< --architecture aarch64 --output $@ \
		--report $(RIBOS_R18_AARCH64_GRAPH)

$(RIBOS_R18_AARCH64_DIR)/obj/%.o: %.c Makefile
	@mkdir -p $(@D)
	$(AARCH64_CC) $(AARCH64_FLAGS) $(DEPFLAGS) \
		$(RIBOS_TARGET_INCLUDE_FLAGS) $(SECURITY_INCLUDE_FLAGS) \
		-ffunction-sections -fdata-sections -c $< -o $@

$(RIBOS_R18_AARCH64_DIR)/obj/generated/plugin_registry.o: \
		$(RIBOS_R18_AARCH64_REGISTRY_C) Makefile
	@mkdir -p $(@D)
	$(AARCH64_CC) $(AARCH64_FLAGS) $(DEPFLAGS) \
		$(RIBOS_TARGET_INCLUDE_FLAGS) -c $< -o $@

$(RIBOS_R18_AARCH64_DIR)/obj/generated/embedded_policy.o: \
		$(RIBOS_R18_EMBED_C) Makefile
	@mkdir -p $(@D)
	$(AARCH64_CC) $(AARCH64_FLAGS) $(DEPFLAGS) -c $< -o $@

$(RIBOS_R18_AARCH64_DIR)/obj/generated/embedded_trial_policy.o: \
		$(RIBOS_R18_TRIAL_EMBED_C) Makefile
	@mkdir -p $(@D)
	$(AARCH64_CC) $(AARCH64_FLAGS) $(DEPFLAGS) -c $< -o $@

$(RIBOS_R18_AARCH64_DIR)/obj/targets/qemu-aarch64-virt-raw-fdt/entry.o: \
		targets/qemu-aarch64-virt-raw-fdt/entry.S
	@mkdir -p $(@D)
	$(AARCH64_CC) --target=aarch64-none-elf -c $< -o $@

$(RIBOS_R18_AARCH64_VM_LIB): $(RIBOS_R18_AARCH64_VM_OBJS) Makefile
	$(RM) $@
	$(LLVM_AR) rcs $@ $(RIBOS_R18_AARCH64_VM_OBJS)

$(RIBOS_R18_AARCH64_ELF): $(RIBOS_R18_AARCH64_OBJS) \
		$(RIBOS_R18_AARCH64_VM_LIB) \
		targets/qemu-aarch64-virt-raw-fdt/linker.ld
	$(LD_LLD) -m aarch64elf --gc-sections \
		-T targets/qemu-aarch64-virt-raw-fdt/linker.ld \
		-Map=$(RIBOS_R18_AARCH64_DIR)/ribon-ribos.map \
		-o $@ $(RIBOS_R18_AARCH64_OBJS) $(RIBOS_R18_AARCH64_VM_LIB)

$(RIBOS_R18_AARCH64_IMAGE): $(RIBOS_R18_AARCH64_ELF)
	$(OBJCOPY) -O binary $< $@

$(RIBOS_R18_RISCV64_REGISTRY_C): \
		$(RIBOS_R18_MANIFEST) tools/generate_plugin_registry.py
	$(PYTHON) tools/generate_plugin_registry.py \
		--manifest $< --architecture riscv64 --output $@ \
		--report $(RIBOS_R18_RISCV64_GRAPH)

$(RIBOS_R18_RISCV64_DIR)/obj/%.o: %.c Makefile
	@mkdir -p $(@D)
	$(RISCV64_CC) $(RISCV64_FLAGS) $(DEPFLAGS) \
		$(RIBOS_TARGET_INCLUDE_FLAGS) $(SECURITY_INCLUDE_FLAGS) \
		-ffunction-sections -fdata-sections -c $< -o $@

$(RIBOS_R18_RISCV64_DIR)/obj/generated/plugin_registry.o: \
		$(RIBOS_R18_RISCV64_REGISTRY_C) Makefile
	@mkdir -p $(@D)
	$(RISCV64_CC) $(RISCV64_FLAGS) $(DEPFLAGS) \
		$(RIBOS_TARGET_INCLUDE_FLAGS) -c $< -o $@

$(RIBOS_R18_RISCV64_DIR)/obj/generated/embedded_policy.o: \
		$(RIBOS_R18_EMBED_C) Makefile
	@mkdir -p $(@D)
	$(RISCV64_CC) $(RISCV64_FLAGS) $(DEPFLAGS) -c $< -o $@

$(RIBOS_R18_RISCV64_DIR)/obj/generated/embedded_trial_policy.o: \
		$(RIBOS_R18_TRIAL_EMBED_C) Makefile
	@mkdir -p $(@D)
	$(RISCV64_CC) $(RISCV64_FLAGS) $(DEPFLAGS) -c $< -o $@

$(RIBOS_R18_RISCV64_DIR)/obj/targets/qemu-riscv64-virt-opensbi/entry.o: \
		targets/qemu-riscv64-virt-opensbi/entry.S
	@mkdir -p $(@D)
	$(RISCV64_CC) --target=riscv64-none-elf -march=rv64gc \
		-mabi=lp64d -mcmodel=medany -c $< -o $@

$(RIBOS_R18_RISCV64_VM_LIB): $(RIBOS_R18_RISCV64_VM_OBJS) Makefile
	$(RM) $@
	$(LLVM_AR) rcs $@ $(RIBOS_R18_RISCV64_VM_OBJS)

$(RIBOS_R18_RISCV64_ELF): $(RIBOS_R18_RISCV64_OBJS) \
		$(RIBOS_R18_RISCV64_VM_LIB) \
		targets/qemu-riscv64-virt-opensbi/linker.ld
	$(LD_LLD) -m elf64lriscv --gc-sections \
		-T targets/qemu-riscv64-virt-opensbi/linker.ld \
		-Map=$(RIBOS_R18_RISCV64_DIR)/ribon-ribos.map \
		-o $@ $(RIBOS_R18_RISCV64_OBJS) $(RIBOS_R18_RISCV64_VM_LIB)

$(RIBOS_R18_RISCV64_IMAGE): $(RIBOS_R18_RISCV64_ELF)
	$(OBJCOPY) -O binary $< $@

$(RIBOS_R18_AMD64_REGISTRY_C): \
		$(RIBOS_R18_MANIFEST) tools/generate_plugin_registry.py
	$(PYTHON) tools/generate_plugin_registry.py \
		--manifest $< --architecture x86_64 --output $@ \
		--report $(RIBOS_R18_AMD64_GRAPH)

$(RIBOS_R18_AMD64_DIR)/obj/%.o: %.c Makefile
	@mkdir -p $(@D)
	/usr/bin/clang $(UEFI_FLAGS) $(DEPFLAGS) \
		$(RIBOS_TARGET_INCLUDE_FLAGS) $(SECURITY_INCLUDE_FLAGS) \
		-ffunction-sections -fdata-sections -c $< -o $@

$(RIBOS_R18_AMD64_DIR)/obj/generated/plugin_registry.o: \
		$(RIBOS_R18_AMD64_REGISTRY_C) Makefile
	@mkdir -p $(@D)
	/usr/bin/clang $(UEFI_FLAGS) $(DEPFLAGS) \
		$(RIBOS_TARGET_INCLUDE_FLAGS) -c $< -o $@

$(RIBOS_R18_AMD64_DIR)/obj/generated/embedded_policy.o: \
		$(RIBOS_R18_EMBED_C) Makefile
	@mkdir -p $(@D)
	/usr/bin/clang $(UEFI_FLAGS) $(DEPFLAGS) -c $< -o $@

$(RIBOS_R18_AMD64_DIR)/obj/generated/embedded_trial_policy.o: \
		$(RIBOS_R18_TRIAL_EMBED_C) Makefile
	@mkdir -p $(@D)
	/usr/bin/clang $(UEFI_FLAGS) $(DEPFLAGS) -c $< -o $@

$(RIBOS_R18_AMD64_VM_LIB): $(RIBOS_R18_AMD64_VM_OBJS) Makefile
	$(RM) $@
	$(LLVM_AR) rcs $@ $(RIBOS_R18_AMD64_VM_OBJS)

$(RIBOS_R18_AMD64_APP): $(RIBOS_R18_AMD64_OBJS) \
		$(RIBOS_R18_AMD64_VM_LIB)
	$(LLD_LINK) /subsystem:efi_application /entry:efi_main /nodefaultlib \
		/brepro /opt:ref \
		/machine:x64 /map:$(RIBOS_R18_AMD64_DIR)/ribon-ribos.map \
		/out:$@ $(RIBOS_R18_AMD64_OBJS) $(RIBOS_R18_AMD64_VM_LIB)

$(RIBOS_R18_AMD64_ESP)/EFI/BOOT/BOOTX64.EFI: $(RIBOS_R18_AMD64_APP)
	@mkdir -p $(@D)
	cp $< $@

check-ribos-cross-arch-objects: \
		$(RIBOS_R18_AMD64_APP) \
		$(RIBOS_R18_AARCH64_IMAGE) \
		$(RIBOS_R18_RISCV64_IMAGE)
	$(PYTHON) tools/lint/ribos_cross_arch_object_lint.py \
		--map $(RIBOS_R18_AMD64_DIR)/ribon-ribos.map \
		--map $(RIBOS_R18_AARCH64_DIR)/ribon-ribos.map \
		--map $(RIBOS_R18_RISCV64_DIR)/ribon-ribos.map \
		--image $(RIBOS_R18_AMD64_APP) \
		--image $(RIBOS_R18_AARCH64_ELF) \
		--image $(RIBOS_R18_RISCV64_ELF)

$(SECURITY_TEST): tests/security/ed25519_provider_tests.c \
		$(SECURITY_PROVIDER_SRCS) include/Ribon/security/signature.h \
		include/Ribon/security/ed25519.h Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(SECURITY_INCLUDE_FLAGS) \
		tests/security/ed25519_provider_tests.c \
		$(SECURITY_PROVIDER_SRCS) -o $@

$(SECURITY_SANITIZER_TEST): tests/security/ed25519_provider_tests.c \
		$(SECURITY_PROVIDER_SRCS) include/Ribon/security/signature.h \
		include/Ribon/security/ed25519.h Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g $(WARNFLAGS) \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$(SECURITY_INCLUDE_FLAGS) tests/security/ed25519_provider_tests.c \
		$(SECURITY_PROVIDER_SRCS) -o $@

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
		include/Ribon/security/key_policy.h Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(SECURITY_INCLUDE_FLAGS) \
		tests/security/key_policy_tests.c $(SECURITY_KEY_POLICY_SRCS) \
		$(SECURITY_PROVIDER_SRCS) -o $@

$(KEY_POLICY_SANITIZER_TEST): tests/security/key_policy_tests.c \
		$(SECURITY_KEY_POLICY_SRCS) $(SECURITY_PROVIDER_SRCS) \
		include/Ribon/security/key_policy.h Makefile
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
		src/security/sha256.h include/Ribon/security/protected_state.h Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) \
		tests/security/protected_state_tests.c \
		$(SECURITY_PROTECTED_STATE_SRCS) -o $@

$(PROTECTED_STATE_SANITIZER_TEST): tests/security/protected_state_tests.c \
		$(SECURITY_PROTECTED_STATE_SRCS) \
		src/security/sha256.h include/Ribon/security/protected_state.h Makefile
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
		$(SECURITY_PROVIDER_SRCS) include/Ribon/update/manifest.h Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(SECURITY_INCLUDE_FLAGS) \
		tests/update/manifest_tests.c $(UPDATE_MANIFEST_SRCS) \
		$(SECURITY_KEY_POLICY_SRCS) $(SECURITY_PROVIDER_SRCS) -o $@

$(UPDATE_MANIFEST_SANITIZER_TEST): tests/update/manifest_tests.c \
		$(UPDATE_MANIFEST_SRCS) $(SECURITY_KEY_POLICY_SRCS) \
		$(SECURITY_PROVIDER_SRCS) include/Ribon/update/manifest.h Makefile
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
		--openssl openssl

check-update-manifest-sanitizer: $(UPDATE_MANIFEST_SANITIZER_TEST)
	$(UPDATE_MANIFEST_SANITIZER_TEST)

check-update-manifest-cross-compile:
	@set -e; \
	for target in x86_64 aarch64 riscv64; do \
		case "$$target" in \
			x86_64) compiler=/usr/bin/clang; triple=x86_64-none-elf;; \
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
		$(SECURITY_PROVIDER_SRCS) include/Ribon/update/storage.h Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(SECURITY_INCLUDE_FLAGS) \
		tests/update/storage_tests.c tests/update/reference_storage.c \
		$(UPDATE_STORAGE_SRCS) \
		$(SECURITY_KEY_POLICY_SRCS) $(SECURITY_PROVIDER_SRCS) -o $@

$(UPDATE_STORAGE_SANITIZER_TEST): tests/update/storage_tests.c \
		tests/update/reference_storage.c tests/update/reference_storage.h \
		$(UPDATE_STORAGE_SRCS) $(SECURITY_KEY_POLICY_SRCS) \
		$(SECURITY_PROVIDER_SRCS) include/Ribon/update/storage.h Makefile
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
			x86_64) compiler=/usr/bin/clang; triple=x86_64-none-elf;; \
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
		include/Ribon/update/installer.h $(UEFI_UPDATE_FIXTURE_STAMP) Makefile
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
		$(UEFI_UPDATE_FIXTURE_STAMP) Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(SECURITY_INCLUDE_FLAGS) \
		tests/update/power_cut_tests.c src/update/transaction.c \
		src/update/installer.c $(UPDATE_STORAGE_SRCS) \
		$(SECURITY_KEY_POLICY_SRCS) $(SECURITY_PROVIDER_SRCS) -o $@

$(UPDATE_POWER_CUT_SANITIZER_TEST): tests/update/power_cut_tests.c \
		src/update/transaction.c src/update/installer.c $(UPDATE_STORAGE_SRCS) \
		$(SECURITY_KEY_POLICY_SRCS) $(SECURITY_PROVIDER_SRCS) \
		include/Ribon/update/transaction.h include/Ribon/update/installer.h \
		$(UEFI_UPDATE_FIXTURE_STAMP) Makefile
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
			x86_64) compiler=/usr/bin/clang; triple=x86_64-none-elf;; \
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
		src/environments/uefi-app/update_storage.h $(UEFI_UPDATE_FIXTURE_STAMP) Makefile
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
		include/Ribon/network/tftp.h include/Ribon/service/directory.h Makefile
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) \
		tests/network/recovery_network_tests.c \
		src/common/net/recovery.c src/common/net/tftp.c \
		src/core/service_directory.c -o $@

$(RECOVERY_NETWORK_SANITIZER_TEST): tests/network/recovery_network_tests.c \
		src/common/net/recovery.c src/common/net/tftp.c \
		src/core/service_directory.c include/Ribon/network/recovery.h \
		include/Ribon/network/tftp.h include/Ribon/service/directory.h Makefile
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
		x86_64) compiler=/usr/bin/clang; triple=x86_64-none-elf;; \
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
			x86_64) compiler=/usr/bin/clang; triple=x86_64-none-elf;; \
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
	$(RISCV64_CC) $(RISCV64_FLAGS) $(DEPFLAGS) -c $< -o $@

$(QEMU_RISCV64_DIR)/obj/generated/plugin_registry.o: $(QEMU_RISCV64_REGISTRY_C)
	@mkdir -p $(@D)
	$(RISCV64_CC) $(RISCV64_FLAGS) $(DEPFLAGS) -c $< -o $@

$(QEMU_RISCV64_DIR)/obj/generated/embedded_payload.o: $(QEMU_RISCV64_EMBED_C)
	@mkdir -p $(@D)
	$(RISCV64_CC) $(RISCV64_FLAGS) $(DEPFLAGS) -c $< -o $@

$(QEMU_RISCV64_DIR)/obj/targets/qemu-riscv64-virt-opensbi/entry.o: \
	targets/qemu-riscv64-virt-opensbi/entry.S
	@mkdir -p $(@D)
	$(RISCV64_CC) --target=riscv64-none-elf -march=rv64gc \
		-mabi=lp64d -mcmodel=medany -c $< -o $@

$(QEMU_RISCV64_RPH1_FIXTURE_DIR)/obj/tests/fixtures/riscv64/rph1_consumer.o: \
	tests/fixtures/riscv64/rph1_consumer.c
	@mkdir -p $(@D)
	$(RISCV64_CC) $(RISCV64_FLAGS) $(DEPFLAGS) -c $< -o $@

$(QEMU_RISCV64_RPH1_FIXTURE_DIR)/obj/tests/fixtures/riscv64/rph1_consumer_entry.o: \
	tests/fixtures/riscv64/rph1_consumer_entry.S
	@mkdir -p $(@D)
	$(RISCV64_CC) --target=riscv64-none-elf -march=rv64gc \
		-mabi=lp64d -mcmodel=medany -c $< -o $@

$(QEMU_RISCV64_RPH1_FIXTURE_PAYLOAD): \
	$(QEMU_RISCV64_RPH1_FIXTURE_OBJS) \
	tests/fixtures/riscv64/rph1_consumer.ld
	$(LD_LLD) -m elf64lriscv \
		-T tests/fixtures/riscv64/rph1_consumer.ld \
		-Map=$(QEMU_RISCV64_RPH1_FIXTURE_DIR)/payload.map \
		-o $@ $(QEMU_RISCV64_RPH1_FIXTURE_OBJS)

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

qemu-riscv64-virt-rph1-fixture-product: \
	$(QEMU_RISCV64_RPH1_FIXTURE_PAYLOAD)
	$(MAKE) --no-print-directory \
		QEMU_RISCV64_DIR=$(abspath $(QEMU_RISCV64_RPH1_FIXTURE_DIR)) \
		QEMU_RISCV64_MANIFEST=$(QEMU_RISCV64_RPH1_FIXTURE_MANIFEST) \
		QEMU_RISCV64_PAYLOAD=$(abspath $(QEMU_RISCV64_RPH1_FIXTURE_PAYLOAD)) \
		$(abspath $(QEMU_RISCV64_RPH1_FIXTURE_IMAGE))

qemu-riscv64-virt-rph1-fixture-smoke: \
	qemu-riscv64-virt-rph1-fixture-product
	$(PYTHON) tools/qemu_target_smoke.py \
		--target riscv64-virt-opensbi --qemu $(QEMU_RISCV64) \
		--image $(QEMU_RISCV64_RPH1_FIXTURE_IMAGE) \
		--firmware $(RISCV64_OPENSBI_FIRMWARE) \
		--payload $(QEMU_RISCV64_RPH1_FIXTURE_PAYLOAD) \
		--expected-payload-class fixture \
		--product-manifest $(QEMU_RISCV64_RPH1_FIXTURE_MANIFEST) \
		--required-marker RIBON-RPH1-RISCV64-FIXTURE-ENTRY \
		--required-marker RIBON-RPH1-RISCV64-FIXTURE-MMU-OFF \
		--required-marker RIBON-RPH1-RISCV64-FIXTURE-RPH1-OK \
		--required-marker RIBON-RPH1-RISCV64-FIXTURE-BOOT-CPU-OK \
		--source-revision $(shell git rev-parse HEAD) \
		--log $(RESULTS_DIR)/qemu-riscv64-virt-rph1-fixture.log \
		--result $(RESULTS_DIR)/qemu-riscv64-virt-rph1-fixture.json

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

$(UEFI_LINUX_INPUT_MANIFEST): $(UEFI_LINUX_MANIFEST)
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

$(UEFI_FIXTURE_PAYLOAD_SOURCE): tools/make_elf64_fixture.py
	@mkdir -p $(@D)
	$(PYTHON) $< --arch x86_64 --base 0x200000 --entry-at-base --output $@

$(UEFI_FIXTURE_DIR)/obj/%.o: %.c Makefile
	@mkdir -p $(@D)
	/usr/bin/clang $(UEFI_FLAGS) $(DEPFLAGS) -c $< -o $@

$(UEFI_EXTERNAL_DIR)/obj/%.o: %.c Makefile
	@mkdir -p $(@D)
	/usr/bin/clang $(UEFI_FLAGS) $(DEPFLAGS) -c $< -o $@

$(UEFI_LINUX_DIR)/obj/%.o: %.c Makefile
	@mkdir -p $(@D)
	/usr/bin/clang $(UEFI_FLAGS) $(DEPFLAGS) -c $< -o $@

$(UEFI_FIXTURE_DIR)/obj/generated/plugin_registry.o: \
	$(UEFI_FIXTURE_REGISTRY_C)
	@mkdir -p $(@D)
	/usr/bin/clang $(UEFI_FLAGS) $(DEPFLAGS) -c $< -o $@

$(UEFI_EXTERNAL_DIR)/obj/generated/plugin_registry.o: \
	$(UEFI_EXTERNAL_REGISTRY_C)
	@mkdir -p $(@D)
	/usr/bin/clang $(UEFI_FLAGS) $(DEPFLAGS) -c $< -o $@

$(UEFI_LINUX_DIR)/obj/generated/plugin_registry.o: $(UEFI_LINUX_REGISTRY_C)
	@mkdir -p $(@D)
	/usr/bin/clang $(UEFI_FLAGS) $(DEPFLAGS) -c $< -o $@

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

$(UEFI_FIXTURE_ESP)/EFI/BOOT/BOOTX64.EFI: $(UEFI_FIXTURE_APP)
	@mkdir -p $(@D)
	cp $< $@

$(UEFI_EXTERNAL_ESP)/EFI/BOOT/BOOTX64.EFI: $(UEFI_EXTERNAL_APP)
	@mkdir -p $(@D)
	cp $< $@

$(UEFI_LINUX_ESP)/EFI/BOOT/BOOTX64.EFI: $(UEFI_LINUX_APP)
	@mkdir -p $(@D)
	cp $< $@

$(UEFI_FIXTURE_CONFIG): tools/make_boot_config.py Makefile
	@mkdir -p $(@D)
	$(PYTHON) $< --output $@ --entry primary --priority 100 \
		--protocol protocol.parus --image image.elf64 --kernel /RIBON/PAYLOAD.ELF \
		--init-image /RIBON/INIT.IMG

$(UEFI_EXTERNAL_CONFIG): tools/make_boot_config.py Makefile
	@mkdir -p $(@D)
	$(PYTHON) $< --output $@ --entry primary --priority 100 \
		--protocol protocol.parus --image image.elf64 --kernel /RIBON/PAYLOAD.ELF \
		--init-image /RIBON/INIT.IMG

$(UEFI_LINUX_CONFIG): tools/make_boot_config.py Makefile
	@mkdir -p $(@D)
	$(PYTHON) $< --output $@ --entry primary --priority 100 \
		--protocol protocol.linux-efi --image image.pe-coff \
		--kernel /RIBON/LINUX.EFI \
		--cmdline 'console=ttyS0 rdinit=/init panic=-1 initrd=\RIBON\INITRD.CPIO'

$(UEFI_LINUX_PAYLOAD): $(UEFI_LINUX_EXTERNAL_STAMP)
	@mkdir -p $(@D)
	cp $(UEFI_LINUX_CACHE) $@

$(UEFI_LINUX_INIT_OBJ): tests/fixtures/linux/x86_64/init.S
	@mkdir -p $(@D)
	/usr/bin/clang --target=x86_64-none-elf -c $< -o $@

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

$(UEFI_UPDATE_DIR)/obj/%.o: %.c Makefile
	@mkdir -p $(@D)
	/usr/bin/clang $(UEFI_FLAGS) $(SECURITY_INCLUDE_FLAGS) $(DEPFLAGS) -c $< -o $@

$(UEFI_UPDATE_DIR)/obj/generated/plugin_registry.o: $(UEFI_UPDATE_REGISTRY_C)
	@mkdir -p $(@D)
	/usr/bin/clang $(UEFI_FLAGS) $(SECURITY_INCLUDE_FLAGS) $(DEPFLAGS) -c $< -o $@

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
		include/Ribon/update/confirmation.h Makefile
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
		include/Ribon/update/confirmation.h Makefile
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
		x86_64) compiler=/usr/bin/clang; triple=x86_64-none-elf;; \
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

$(UEFI_NETWORK_UPDATE_DIR)/obj/%.o: %.c Makefile
	@mkdir -p $(@D)
	/usr/bin/clang $(UEFI_FLAGS) $(SECURITY_INCLUDE_FLAGS) $(DEPFLAGS) -c $< -o $@

$(UEFI_NETWORK_UPDATE_DIR)/obj/generated/plugin_registry.o: \
	$(UEFI_NETWORK_UPDATE_REGISTRY_C)
	@mkdir -p $(@D)
	/usr/bin/clang $(UEFI_FLAGS) $(SECURITY_INCLUDE_FLAGS) $(DEPFLAGS) -c $< -o $@

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

check-terminal-image-launch: check-boot-lifecycle check-core-service
	@echo "RIBON-R02-TERMINAL-IMAGE-LAUNCH-OK"

check-uefi-managed-image: x86_64-uefi-linux-product x86_64-uefi-parus-fixture
	@! grep -q ribon_uefi_app_terminal_image_launch_service_descriptor \
		$(UEFI_FIXTURE_DIR)/ribon.map
	@grep -q ribon_uefi_app_terminal_image_launch_service_descriptor \
		$(UEFI_LINUX_DIR)/ribon.map
	@echo "RIBON-R02-UEFI-MANAGED-IMAGE-OK"

check-linux-x86_64-efi: x86_64-uefi-linux-smoke
	$(PYTHON) tests/tools/external_linux_efi_tests.py
	@echo "RIBON-R02-LINUX-X86_64-EFI-OK"

$(BIOS_REGISTRY_C): $(BIOS_MANIFEST) tools/generate_plugin_registry.py
	$(PYTHON) tools/generate_plugin_registry.py --manifest $< \
		--output $@ --report $(BIOS_GRAPH)

$(BIOS_DIR)/obj/%.o: %.c
	@mkdir -p $(@D)
	/usr/bin/clang $(BIOS_FLAGS) $(DEPFLAGS) -c $< -o $@

bios-compile: $(BIOS_REGISTRY_C) $(BIOS_OBJECTS)
	@echo "RIBON-R4-BIOS-COMPILE-ONLY-OK"

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

check-library-embed: $(PROTOCOL_FREE_EMBED_TEST)
	$(PROTOCOL_FREE_EMBED_TEST)

check-composition-schemas:
	$(PYTHON) tools/lint/composition_schema_lint.py

check-qemu-evidence:
	$(PYTHON) tests/tools/qemu_target_smoke_tests.py
	$(PYTHON) tests/tools/external_parus_payload_tests.py
	$(PYTHON) tests/tools/external_linux_image_tests.py

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
		--makefile Makefile \
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
	qemu-riscv64-virt-rph1-fixture-product x86_64-uefi-parus-fixture \
	x86_64-uefi-linux-product x86_64-uefi-network-update-recovery
	$(PYTHON) tools/lint/target_object_graph_lint.py $(TARGET_BUILD_ROOT)

check-uefi-product-hermeticity:
	$(PYTHON) tools/check_uefi_product_hermeticity.py \
		--make $(MAKE) --root $(ROOT) \
		--work-root $(BUILD_ROOT)/tests/uefi-product-hermeticity

check: legacy-hard-cut check-public-api check-frontends check-loader \
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
	check-external-plugin check-firmware-personalities \
	check-firmware-object-graphs check-object-graphs check-normal-media-surface qstar-check
	@for arch in $(RIBON_ARCHES); do \
		$(MAKE) --no-print-directory RIBON_ARCH=$$arch check-one || exit $$?; \
	done
	@echo "RIBON-R5-AGGREGATE-OK"

docs: legacy-hard-cut
	$(PYTHON) tools/lint/documentation_quality_lint.py
	@mkdir -p $(BUILD_ROOT)/docs
	$(DOXYGEN) docs/Doxyfile
	$(SPHINX_BUILD) -W --keep-going -b html docs $(BUILD_ROOT)/docs/html

docs-lint: legacy-hard-cut
	$(PYTHON) tools/lint/documentation_quality_lint.py

docs-clean:
	rm -rf $(BUILD_ROOT)/docs

clean:
	rm -rf $(BUILD_ROOT)
