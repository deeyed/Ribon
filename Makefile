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
QEMU_AARCH64 ?= /opt/homebrew/bin/qemu-system-aarch64
QEMU_X86_64 ?= /opt/homebrew/bin/qemu-system-x86_64
QEMU_RISCV64 ?= /opt/homebrew/bin/qemu-system-riscv64
RISCV64_OPENSBI_FIRMWARE ?= /opt/homebrew/Cellar/qemu/11.0.2/share/qemu/opensbi-riscv64-generic-fw_dynamic.bin
X86_64_UEFI_FIRMWARE ?= /opt/homebrew/Cellar/qemu/11.0.2/share/qemu/edk2-x86_64-code.fd

CFLAGS ?= -std=c11 -O2 -g
WARNFLAGS := -Wall -Wextra -Werror
DEPFLAGS := -MMD -MP
CPPFLAGS += -I$(ROOT)/include
FREESTANDING_FLAGS := -std=c11 -O2 -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables \
	-Wall -Wextra -Werror -I$(ROOT)/include/freestanding -I$(ROOT)/include
AARCH64_FLAGS := --target=aarch64-none-elf $(FREESTANDING_FLAGS) -mgeneral-regs-only
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
HOST_REFERENCE := $(BUILD_DIR)/ribon-host-reference
KERNEL_FIXTURE := $(BUILD_DIR)/fixtures/kernel.elf
HOST_MANIFEST := qstar/manifests/host-reference.json
GENERATED_REGISTRY_C := $(BUILD_DIR)/generated/plugin_registry.c
GENERATED_REGISTRY_REPORT := $(BUILD_DIR)/results/host-reference-object-graph.json
GENERATED_REGISTRY_O := $(BUILD_DIR)/obj/generated/plugin_registry.o
TEST_BUILD_DIR := $(BUILD_ROOT)/tests
TARGET_BUILD_ROOT := $(BUILD_ROOT)/targets
RESULTS_DIR := $(BUILD_ROOT)/results
RIBOS_PARSER_PILOT := $(BUILD_ROOT)/tools/ribos-parse
RIBOS_PEGEN_ROOT ?=
RIBOS_PARSER_SRCS := \
	language/src/ribos_lexer.c \
	language/src/ribos_runtime.c \
	language/src/ribos_parser.c \
	language/generated/ribos_parser.c \
	language/tools/ribos_parse.c
RIBOS_PARSER_HEADERS := \
	language/include/ribos/parser.h \
	language/src/ribos_parser_internal.h \
	language/generated/ribos_tokens.h

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
RPH1_TEST := $(TEST_BUILD_DIR)/rph1_builder_tests
ARCH_X86_64_TEST := $(TEST_BUILD_DIR)/x86_64_direct_high_tests
ARCH_AARCH64_TEST := $(TEST_BUILD_DIR)/aarch64_direct_high_tests
ARCH_OPS_TESTS := $(RIBON_ARCHES:%=$(TEST_BUILD_DIR)/arch_ops_%_tests)
CORE_SERVICE_TEST := $(TEST_BUILD_DIR)/core_service_boundary_tests
PORT_SERVICE_TEST := $(TEST_BUILD_DIR)/port_service_tests
BOOT_LIFECYCLE_TEST := $(TEST_BUILD_DIR)/boot_lifecycle_tests
ENVIRONMENT_PERSISTENT_INPUTS_TEST := $(TEST_BUILD_DIR)/environment_persistent_inputs_tests
MEDIA_PIPELINE_TEST := $(TEST_BUILD_DIR)/media_pipeline_tests
PLUGIN_DESCRIPTOR_TEST := $(TEST_BUILD_DIR)/plugin_descriptor_tests
PROTOCOL_CONTRACT_TEST := $(TEST_BUILD_DIR)/protocol_contract_tests
PARUS_ENTRY_CONTRACT_TEST := $(TEST_BUILD_DIR)/parus_entry_contract_tests
OS_PACKAGE_TEST := $(TEST_BUILD_DIR)/os_package_tests
PROTOCOL_FREE_EMBED_TEST := $(TEST_BUILD_DIR)/protocol_free_embed_tests
SDK_INSTALL_ROOT := $(BUILD_ROOT)/sdk/install
SDK_REPRO_FIRST := $(BUILD_ROOT)/sdk/reproducible-a
SDK_REPRO_SECOND := $(BUILD_ROOT)/sdk/reproducible-b
SDK_LIBRARY_EMBED_TEST := $(BUILD_ROOT)/sdk/examples/library-embed
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
	src/image-formats/elf64.c \
	src/protocols/os/parus/protocol.c \
	src/protocols/os/parus/rph1_builder.c \
	src/protocols/os/parus/rph1_parser.c \
	src/environments/raw-fdt/raw_fdt.c \
	products/bootmgr/raw_fdt_main.c

QEMU_RAW_DIR := $(TARGET_BUILD_ROOT)/qemu-aarch64-virt-raw-fdt
QEMU_RAW_MANIFEST := products/bootmgr/manifests/qemu-aarch64-virt-parus.json
QEMU_RAW_REGISTRY_C := $(QEMU_RAW_DIR)/generated/plugin_registry.c
QEMU_RAW_GRAPH := $(QEMU_RAW_DIR)/results/object-graph.json
QEMU_RAW_FIXTURE := $(QEMU_RAW_DIR)/payload.elf
QEMU_RAW_PAYLOAD ?= $(QEMU_RAW_FIXTURE)
QEMU_RAW_EMBED_C := $(QEMU_RAW_DIR)/generated/embedded_payload.c
QEMU_RAW_ELF := $(QEMU_RAW_DIR)/ribon.elf
QEMU_RAW_IMAGE := $(QEMU_RAW_DIR)/ribon.bin
QEMU_RAW_SRCS := $(RAW_COMMON_SRCS) \
	src/common/drivers/serial/pl011.c \
	src/arch/aarch64/arch.c \
	ports/qemu/virt-aarch64/port.c
QEMU_RAW_OBJS := $(QEMU_RAW_SRCS:%.c=$(QEMU_RAW_DIR)/obj/%.o)
QEMU_RAW_OBJS += \
	$(QEMU_RAW_DIR)/obj/generated/plugin_registry.o \
	$(QEMU_RAW_DIR)/obj/generated/embedded_payload.o \
	$(QEMU_RAW_DIR)/obj/targets/qemu-aarch64-virt-raw-fdt/entry.o

QEMU_PARUS_DIR := $(TARGET_BUILD_ROOT)/qemu-aarch64-virt-parus
QEMU_PARUS_MANIFEST := products/bootmgr/manifests/qemu-aarch64-virt-parus-external.json
QEMU_PARUS_PAYLOAD ?=
QEMU_PARUS_IMAGE := $(QEMU_PARUS_DIR)/ribon.bin
QEMU_PARUS_VALIDATION := $(QEMU_PARUS_DIR)/results/external-payload.json

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

UEFI_DIR := $(TARGET_BUILD_ROOT)/x86_64-uefi-app
UEFI_MANIFEST := products/bootmgr/manifests/x86_64-uefi-parus.json
UEFI_EXTERNAL_MANIFEST := products/bootmgr/manifests/x86_64-uefi-parus-external.json
UEFI_PARUS_PAYLOAD ?=
UEFI_PARUS_VALIDATION := $(UEFI_DIR)/results/external-payload.json
ifneq ($(strip $(UEFI_PARUS_PAYLOAD)),)
UEFI_SELECTED_MANIFEST = $(UEFI_EXTERNAL_MANIFEST)
UEFI_SELECTED_PAYLOAD = $(abspath $(UEFI_PARUS_PAYLOAD))
UEFI_SELECTED_VALIDATION = $(UEFI_PARUS_VALIDATION)
else
UEFI_SELECTED_MANIFEST = $(UEFI_MANIFEST)
UEFI_SELECTED_PAYLOAD = $(UEFI_FIXTURE)
UEFI_SELECTED_VALIDATION =
endif
UEFI_REGISTRY_C := $(UEFI_DIR)/generated/plugin_registry.c
UEFI_GRAPH := $(UEFI_DIR)/results/object-graph.json
UEFI_FIXTURE := $(UEFI_DIR)/payload.elf
UEFI_APP := $(UEFI_DIR)/BOOTX64.EFI
UEFI_ESP := $(UEFI_DIR)/esp
UEFI_CONFIG := $(UEFI_ESP)/RIBON/BOOT.CFG
UEFI_PAYLOAD := $(UEFI_ESP)/RIBON/PAYLOAD.ELF
UEFI_INIT_IMAGE_FIXTURE := $(UEFI_DIR)/fixtures/init-image.bin
UEFI_INIT_IMAGE := $(UEFI_ESP)/RIBON/INIT.IMG
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
UEFI_OBJS := $(UEFI_SRCS:%.c=$(UEFI_DIR)/obj/%.o)
UEFI_OBJS += \
	$(UEFI_DIR)/obj/generated/plugin_registry.o

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
	check-environment-persistent-inputs check-media-pipeline \
	check-mode-descriptors check-plugin-descriptors check-protocol-contract \
	check-parus-entry-contract \
	check-os-packages \
	check-library-embed check-object-graphs check-public-api \
	check-composition-schemas check-sdk-surface check-sdk-embed \
	check-qemu-evidence \
	check-sdk-reproducible check-external-plugin check-firmware-personalities \
	check-firmware-object-graphs firmware-provider-reference \
	check-frontends check-normal-media-surface check-target-builds qemu-aarch64-virt-raw-fdt \
	ribos-parser-pilot check-ribos-parser-snapshot check-ribos-parser-pilot \
	ribos-parser-generate ribos-parser-regenerate-check \
	qemu-aarch64-virt-raw-fdt-smoke qemu-aarch64-virt-parus-product \
	qemu-aarch64-virt-parus-smoke x86_64-uefi-app \
	qemu-riscv64-virt-parus-product qemu-riscv64-virt-parus-smoke \
	qemu-riscv64-virt-rph1-fixture-product \
	qemu-riscv64-virt-rph1-fixture-smoke \
	x86_64-uefi-app-smoke x86_64-uefi-parus-smoke \
	bios-compile rpi5-aarch64-raw-fdt-package \
	rpi5-aarch64-parus-package \
	legacy-hard-cut qstar-check docs docs-lint docs-clean clean

all: lib host-reference

lib: $(CORE_LIB) $(BOOT_LIB) $(SDK_LIB)

host-reference: $(HOST_REFERENCE)

$(RIBOS_PARSER_PILOT): $(RIBOS_PARSER_SRCS) $(RIBOS_PARSER_HEADERS) Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) \
		-Ilanguage/include -Ilanguage/src -Ilanguage/generated \
		$(RIBOS_PARSER_SRCS) -o $@

ribos-parser-pilot: $(RIBOS_PARSER_PILOT)

check-ribos-parser-snapshot:
	$(PYTHON) tools/ribosc/check_parser_snapshot.py

check-ribos-parser-pilot: check-ribos-parser-snapshot $(RIBOS_PARSER_PILOT)
	$(PYTHON) tests/language/parser_pilot_tests.py \
		--parser $(RIBOS_PARSER_PILOT)
	$(RIBOS_PARSER_PILOT) tests/language/positive/full_boot_policy.ribos

# Generation is intentionally explicit. Normal builds compile and validate the
# tracked snapshot without importing or invoking Pegen.
ribos-parser-generate:
	@test -n "$(RIBOS_PEGEN_ROOT)" || \
		{ echo "RIBOS_PEGEN_ROOT must name the pinned CPython Pegen root"; exit 2; }
	$(PYTHON) tools/ribosc/generate_parser.py \
		--pegen-root $(RIBOS_PEGEN_ROOT)

ribos-parser-regenerate-check:
	@test -n "$(RIBOS_PEGEN_ROOT)" || \
		{ echo "RIBOS_PEGEN_ROOT must name the pinned CPython Pegen root"; exit 2; }
	$(PYTHON) tools/ribosc/generate_parser.py \
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
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(DEPFLAGS) -c $< -o $@

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

$(HOST_REFERENCE): \
	$(HOST_MAIN_OBJ) $(ARCH_OBJS) $(HOST_PRODUCT_OBJS) \
	$(GENERATED_REGISTRY_O) $(BOOT_LIB) $(CORE_LIB)
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

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
	$(BOOT_LIB) $(CORE_LIB)
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
	$(BOOT_LIB) $(CORE_LIB)
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(ENVIRONMENT_PERSISTENT_INPUTS_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/boot/environment_persistent_inputs_tests.o \
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
	$(TEST_BUILD_DIR)/obj/src/protocols/os/linux/protocol.o \
	$(TEST_BUILD_DIR)/obj/src/protocols/os/freebsd/protocol.o \
	$(TEST_BUILD_DIR)/obj/src/protocols/os/zircon/protocol.o \
	$(TEST_BUILD_DIR)/obj/src/common/protocol.o
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

$(QEMU_RAW_DIR)/obj/%.o: %.c
	@mkdir -p $(@D)
	$(AARCH64_CC) $(AARCH64_FLAGS) $(DEPFLAGS) -c $< -o $@

$(QEMU_RAW_DIR)/obj/generated/plugin_registry.o: $(QEMU_RAW_REGISTRY_C)
	@mkdir -p $(@D)
	$(AARCH64_CC) $(AARCH64_FLAGS) $(DEPFLAGS) -c $< -o $@

$(QEMU_RAW_DIR)/obj/generated/embedded_payload.o: $(QEMU_RAW_EMBED_C)
	@mkdir -p $(@D)
	$(AARCH64_CC) $(AARCH64_FLAGS) $(DEPFLAGS) -c $< -o $@

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
	tools/validate_external_parus_payload.py
	$(PYTHON) tools/validate_external_parus_payload.py \
		--manifest $(RPI5_EXTERNAL_MANIFEST) \
		--payload $(RPI5_SELECTED_PAYLOAD) \
		--result $@

$(RPI5_REGISTRY_C): $(RPI5_SELECTED_MANIFEST) tools/generate_plugin_registry.py
	$(PYTHON) tools/generate_plugin_registry.py --manifest $< \
		--output $@ --report $(RPI5_GRAPH)

$(RPI5_FIXTURE): tools/make_elf64_fixture.py
	@mkdir -p $(@D)
	$(PYTHON) $< --arch aarch64 --base 0x4000000 --entry-at-base --output $@

$(RPI5_EMBED_C): \
	$(RPI5_SELECTED_PAYLOAD) $(RPI5_SELECTED_VALIDATION) tools/embed_binary.py
	$(PYTHON) tools/embed_binary.py --input $< --output $@

$(RPI5_DIR)/obj/%.o: %.c
	@mkdir -p $(@D)
	$(AARCH64_CC) $(AARCH64_FLAGS) $(DEPFLAGS) -c $< -o $@

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

rpi5-aarch64-raw-fdt-package: $(RPI5_IMAGE) $(RPI5_SELECTED_PAYLOAD)
	$(PYTHON) tools/package_rpi5.py \
		--image $(RPI5_IMAGE) --payload $(RPI5_SELECTED_PAYLOAD) \
		--config targets/rpi5-aarch64-raw-fdt/package/config.txt \
		--cmdline targets/rpi5-aarch64-raw-fdt/package/cmdline.txt \
		--output $(RPI5_PACKAGE)
	$(PYTHON) tools/check_rpi_package.py $(RPI5_PACKAGE)

rpi5-aarch64-parus-package:
	@test -n "$(RPI5_PARUS_PAYLOAD)" || \
		{ echo "RPI5_PARUS_PAYLOAD is required" >&2; exit 2; }
	$(MAKE) --no-print-directory \
		RPI5_PARUS_PAYLOAD=$(abspath $(RPI5_PARUS_PAYLOAD)) \
		rpi5-aarch64-raw-fdt-package

$(UEFI_PARUS_VALIDATION): \
	$(UEFI_EXTERNAL_MANIFEST) $(UEFI_SELECTED_PAYLOAD) \
	tools/validate_external_parus_payload.py
	$(PYTHON) tools/validate_external_parus_payload.py \
		--manifest $(UEFI_EXTERNAL_MANIFEST) \
		--payload $(UEFI_SELECTED_PAYLOAD) \
		--result $@

$(UEFI_REGISTRY_C): $(UEFI_SELECTED_MANIFEST) tools/generate_plugin_registry.py
	$(PYTHON) tools/generate_plugin_registry.py --manifest $< \
		--output $@ --report $(UEFI_GRAPH)

$(UEFI_FIXTURE): tools/make_elf64_fixture.py
	@mkdir -p $(@D)
	$(PYTHON) $< --arch x86_64 --base 0x200000 --entry-at-base --output $@

$(UEFI_DIR)/obj/%.o: %.c
	@mkdir -p $(@D)
	/usr/bin/clang $(UEFI_FLAGS) $(DEPFLAGS) -c $< -o $@

$(UEFI_DIR)/obj/generated/plugin_registry.o: $(UEFI_REGISTRY_C)
	@mkdir -p $(@D)
	/usr/bin/clang $(UEFI_FLAGS) $(DEPFLAGS) -c $< -o $@

$(UEFI_APP): $(UEFI_OBJS)
	$(LLD_LINK) /subsystem:efi_application /entry:efi_main /nodefaultlib \
		/machine:x64 /map:$(UEFI_DIR)/ribon.map /out:$@ $(UEFI_OBJS)

$(UEFI_ESP)/EFI/BOOT/BOOTX64.EFI: $(UEFI_APP)
	@mkdir -p $(@D)
	cp $< $@

$(UEFI_CONFIG): tools/make_boot_config.py Makefile
	@mkdir -p $(@D)
	$(PYTHON) $< --output $@ --entry primary --priority 100 \
		--protocol protocol.parus --image image.elf64 --kernel /RIBON/PAYLOAD.ELF \
		--init-image /RIBON/INIT.IMG

$(UEFI_PAYLOAD): $(UEFI_SELECTED_PAYLOAD) $(UEFI_SELECTED_VALIDATION)
	@mkdir -p $(@D)
	cp $< $@

$(UEFI_INIT_IMAGE_FIXTURE): tools/make_init_image_fixture.py
	@mkdir -p $(@D)
	$(PYTHON) $< --output $@

$(UEFI_INIT_IMAGE): $(UEFI_INIT_IMAGE_FIXTURE)
	@mkdir -p $(@D)
	cp $< $@

x86_64-uefi-app: $(UEFI_ESP)/EFI/BOOT/BOOTX64.EFI $(UEFI_CONFIG) $(UEFI_PAYLOAD) $(UEFI_INIT_IMAGE)

x86_64-uefi-app-smoke: x86_64-uefi-app
	$(PYTHON) tools/qemu_target_smoke.py \
		--target x86_64-uefi --qemu $(QEMU_X86_64) \
		--esp $(UEFI_ESP) --firmware $(X86_64_UEFI_FIRMWARE) \
		--payload $(UEFI_PAYLOAD) --expected-payload-class fixture \
		--init-image $(UEFI_INIT_IMAGE) \
		--source-revision $(shell git rev-parse HEAD) \
		--log $(RESULTS_DIR)/qemu-x86_64-uefi.log \
		--result $(RESULTS_DIR)/qemu-x86_64-uefi.json

x86_64-uefi-parus-smoke:
	@test -n "$(UEFI_PARUS_PAYLOAD)" || \
		{ echo "UEFI_PARUS_PAYLOAD is required" >&2; exit 2; }
	$(MAKE) --no-print-directory \
		UEFI_PARUS_PAYLOAD=$(abspath $(UEFI_PARUS_PAYLOAD)) \
		x86_64-uefi-app
	$(PYTHON) tools/qemu_target_smoke.py \
		--target x86_64-uefi --qemu $(QEMU_X86_64) \
		--esp $(UEFI_ESP) --firmware $(X86_64_UEFI_FIRMWARE) \
		--payload $(UEFI_PARUS_PAYLOAD) --expected-payload-class kernel \
		--init-image $(UEFI_INIT_IMAGE) \
		--product-manifest $(UEFI_EXTERNAL_MANIFEST) \
		$(PARUS_SUCCESS_MARKER_ARGS) \
		--source-revision $(shell git rev-parse HEAD) \
		--log $(RESULTS_DIR)/qemu-x86_64-uefi-parus.log \
		--result $(RESULTS_DIR)/qemu-x86_64-uefi-parus.json

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

check-media-pipeline: $(MEDIA_PIPELINE_TEST)
	$(MEDIA_PIPELINE_TEST) --fuzz-smoke

check-normal-media-surface: x86_64-uefi-app
	$(PYTHON) tools/lint/normal_media_surface_lint.py $(UEFI_DIR)/ribon.map

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

check-library-embed: $(PROTOCOL_FREE_EMBED_TEST)
	$(PROTOCOL_FREE_EMBED_TEST)

check-composition-schemas:
	$(PYTHON) tools/lint/composition_schema_lint.py

check-qemu-evidence:
	$(PYTHON) tests/tools/qemu_target_smoke_tests.py
	$(PYTHON) tests/tools/external_parus_payload_tests.py

sdk-install: lib
	$(PYTHON) tools/install_sdk.py \
		--root $(SDK_INSTALL_ROOT) \
		--public-include include/Ribon \
		--library $(CORE_LIB) \
		--library $(BOOT_LIB) \
		--library $(SDK_LIB) \
		--schemas qstar/schemas \
		--templates sdk/templates

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

check-sdk-reproducible: lib
	$(PYTHON) tools/install_sdk.py \
		--root $(SDK_REPRO_FIRST) \
		--public-include include/Ribon \
		--library $(CORE_LIB) --library $(BOOT_LIB) --library $(SDK_LIB) \
		--schemas qstar/schemas --templates sdk/templates
	$(PYTHON) tools/install_sdk.py \
		--root $(SDK_REPRO_SECOND) \
		--public-include include/Ribon \
		--library $(CORE_LIB) --library $(BOOT_LIB) --library $(SDK_LIB) \
		--schemas qstar/schemas --templates sdk/templates
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

check-object-graphs: $(CORE_LIB) $(BOOT_LIB) $(SDK_LIB)
	$(PYTHON) tools/lint/object_graph_lint.py \
		--core $(CORE_LIB) --boot $(BOOT_LIB) --sdk $(SDK_LIB)

qstar-check:
	$(QSTAR) --file qstar.lua check

check-one: $(HOST_REFERENCE) $(KERNEL_FIXTURE)
	$(HOST_REFERENCE) --kernel $(KERNEL_FIXTURE) > $(BUILD_DIR)/host-reference.txt
	grep -q "product=host-reference" $(BUILD_DIR)/host-reference.txt
	grep -q "registry-plugins=4" $(BUILD_DIR)/host-reference.txt
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
	qemu-aarch64-virt-raw-fdt x86_64-uefi-app
	$(PYTHON) tools/lint/target_object_graph_lint.py $(TARGET_BUILD_ROOT)

check: legacy-hard-cut check-public-api check-frontends check-loader \
	check-ribos-parser-pilot \
	check-pe-coff check-fdt check-rph1 check-arch-x86_64 \
	check-arch-aarch64 check-arch-ops \
	check-core-service check-port-services check-boot-lifecycle \
	check-environment-persistent-inputs check-media-pipeline check-mode-descriptors check-plugin-descriptors \
	check-protocol-contract check-parus-entry-contract check-os-packages \
	check-library-embed check-composition-schemas \
	check-qemu-evidence \
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
