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

CFLAGS ?= -std=c11 -O2 -g
WARNFLAGS := -Wall -Wextra -Werror
CPPFLAGS += -I$(ROOT)/include

ifeq ($(filter $(RIBON_ARCH),$(RIBON_ARCHES)),)
$(error unsupported RIBON_ARCH=$(RIBON_ARCH); supported: $(RIBON_ARCHES))
endif

CORE_LIB := $(BUILD_DIR)/libribon-core.a
BOOT_LIB := $(BUILD_DIR)/libribon-boot.a
HOST_REFERENCE := $(BUILD_DIR)/ribon-host-reference
KERNEL_FIXTURE := $(BUILD_DIR)/fixtures/kernel.elf
GENERATED_MANIFEST := qstar/manifests/host-reference.json
GENERATED_REGISTRY_C := $(BUILD_DIR)/generated/plugin_registry.c
GENERATED_REGISTRY_O := $(BUILD_DIR)/obj/generated/plugin_registry.o
TEST_BUILD_DIR := $(BUILD_ROOT)/tests

CORE_SRCS := \
	src/core/arena.c \
	src/core/context.c \
	src/core/plugin.c \
	src/core/registry.c

BOOT_LIB_SRCS := \
	src/core/memory.c \
	src/common/services.c \
	src/common/environment.c \
	src/common/protocol.c \
	src/common/boot.c

ARCH_SRCS := \
	src/arch/common.c \
	src/arch/$(RIBON_ARCH)/arch.c

HOST_PRODUCT_SRCS := \
	src/environments/host/services.c \
	src/protocols/synthetic/protocol.c \
	src/loader/elf64.c \
	src/modes/normal.c

HOST_MAIN_SRC := src/environments/host/main.c
CORE_OBJS := $(CORE_SRCS:%.c=$(BUILD_DIR)/obj/%.o)
BOOT_LIB_OBJS := $(BOOT_LIB_SRCS:%.c=$(BUILD_DIR)/obj/%.o)
ARCH_OBJS := $(ARCH_SRCS:%.c=$(BUILD_DIR)/obj/%.o)
HOST_PRODUCT_OBJS := $(HOST_PRODUCT_SRCS:%.c=$(BUILD_DIR)/obj/%.o)
HOST_MAIN_OBJ := $(BUILD_DIR)/obj/$(HOST_MAIN_SRC:.c=.o)

LOADER_TEST := $(TEST_BUILD_DIR)/elf64_loader_tests
RPH1_TEST := $(TEST_BUILD_DIR)/rph1_builder_tests
ARCH_X86_64_TEST := $(TEST_BUILD_DIR)/x86_64_direct_high_tests
ARCH_AARCH64_TEST := $(TEST_BUILD_DIR)/aarch64_direct_high_tests
ARCH_OPS_TESTS := $(RIBON_ARCHES:%=$(TEST_BUILD_DIR)/arch_ops_%_tests)
UEFI_HARDENING_TEST := $(TEST_BUILD_DIR)/uefi_hardening_tests
CORE_SERVICE_TEST := $(TEST_BUILD_DIR)/core_service_boundary_tests
PLUGIN_DESCRIPTOR_TEST := $(TEST_BUILD_DIR)/plugin_descriptor_tests
PROTOCOL_CONTRACT_TEST := $(TEST_BUILD_DIR)/protocol_contract_tests
PROTOCOL_FREE_EMBED_TEST := $(TEST_BUILD_DIR)/protocol_free_embed_tests
MODE_DESCRIPTOR_TESTS := \
	$(TEST_BUILD_DIR)/mode_descriptor_normal_tests \
	$(TEST_BUILD_DIR)/mode_descriptor_recovery_tests \
	$(TEST_BUILD_DIR)/mode_descriptor_provisioning_tests \
	$(TEST_BUILD_DIR)/mode_descriptor_diagnostic_tests

.PHONY: all lib host-reference check check-one check-loader check-rph1 \
	check-arch-x86_64 check-arch-aarch64 check-arch-ops check-uefi-hardening \
	check-core-service check-mode-descriptors check-plugin-descriptors \
	check-protocol-contract check-library-embed check-object-graphs \
	check-public-api legacy-hard-cut qstar-check docs docs-lint docs-clean clean

all: lib host-reference

lib: $(CORE_LIB) $(BOOT_LIB)

host-reference: $(HOST_REFERENCE)

$(BUILD_DIR)/obj/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) -c $< -o $@

$(TEST_BUILD_DIR)/obj/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) -c $< -o $@

$(GENERATED_REGISTRY_C): $(GENERATED_MANIFEST) tools/generate_plugin_registry.py
	$(PYTHON) tools/generate_plugin_registry.py \
		--manifest $(GENERATED_MANIFEST) \
		--architecture $(RIBON_ARCH) \
		--output $@

$(GENERATED_REGISTRY_O): $(GENERATED_REGISTRY_C)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) -c $< -o $@

$(CORE_LIB): $(CORE_OBJS) Makefile
	@mkdir -p $(@D)
	$(RM) $@
	$(AR) rcs $@ $(CORE_OBJS)

$(BOOT_LIB): $(BOOT_LIB_OBJS) Makefile
	@mkdir -p $(@D)
	$(RM) $@
	$(AR) rcs $@ $(BOOT_LIB_OBJS)

$(HOST_REFERENCE): \
	$(HOST_MAIN_OBJ) \
	$(ARCH_OBJS) \
	$(HOST_PRODUCT_OBJS) \
	$(GENERATED_REGISTRY_O) \
	$(BOOT_LIB) \
	$(CORE_LIB)
	$(CC) $(CFLAGS) $(WARNFLAGS) \
		$(HOST_MAIN_OBJ) \
		$(ARCH_OBJS) \
		$(HOST_PRODUCT_OBJS) \
		$(GENERATED_REGISTRY_O) \
		$(BOOT_LIB) \
		$(CORE_LIB) \
		-o $@

$(KERNEL_FIXTURE): tools/make_elf64_fixture.py
	@mkdir -p $(@D)
	$(PYTHON) $< --arch $(RIBON_ARCH) --output $@

$(LOADER_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/loader/elf64_loader_tests.o \
	$(TEST_BUILD_DIR)/obj/src/loader/elf64.o
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(RPH1_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/rph1/rph1_builder_tests.o \
	$(TEST_BUILD_DIR)/obj/src/protocols/parus/rph1_builder.o \
	$(TEST_BUILD_DIR)/obj/src/protocols/parus/rph1_parser.o \
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

$(UEFI_HARDENING_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/uefi/uefi_hardening_tests.o \
	$(TEST_BUILD_DIR)/obj/src/firmware/uefi/hardening.o
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(CORE_SERVICE_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/core/service_boundary_tests.o \
	$(ARCH_OBJS) \
	$(HOST_PRODUCT_OBJS) \
	$(GENERATED_REGISTRY_O) \
	$(BOOT_LIB) \
	$(CORE_LIB)
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(PLUGIN_DESCRIPTOR_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/plugin/descriptor_tests.o \
	$(CORE_LIB)
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(PROTOCOL_CONTRACT_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/protocol/contract_tests.o \
	$(ARCH_OBJS) \
	$(BUILD_DIR)/obj/src/protocols/synthetic/protocol.o \
	$(BOOT_LIB) \
	$(CORE_LIB)
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(PROTOCOL_FREE_EMBED_TEST): \
	$(TEST_BUILD_DIR)/obj/tests/library/protocol_free_embed_tests.o \
	$(CORE_LIB)
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(TEST_BUILD_DIR)/mode_descriptor_%_tests: \
	$(TEST_BUILD_DIR)/obj/tests/core/mode_descriptor_tests.o \
	$(TEST_BUILD_DIR)/obj/src/modes/%.o \
	$(CORE_LIB)
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

legacy-hard-cut:
	$(PYTHON) tools/lint/legacy_os_hard_cut.py
	$(PYTHON) tools/lint/library_plugin_hard_cut.py

check-public-api:
	$(PYTHON) tools/lint/public_api_layout_lint.py

check-loader: $(LOADER_TEST)
	$(LOADER_TEST)

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

check-uefi-hardening: $(UEFI_HARDENING_TEST)
	$(UEFI_HARDENING_TEST)

check-core-service: $(CORE_SERVICE_TEST)
	$(CORE_SERVICE_TEST)

check-mode-descriptors: $(MODE_DESCRIPTOR_TESTS)
	@for test_binary in $(MODE_DESCRIPTOR_TESTS); do \
		$$test_binary || exit $$?; \
	done

check-plugin-descriptors: $(PLUGIN_DESCRIPTOR_TEST)
	$(PLUGIN_DESCRIPTOR_TEST)

check-protocol-contract: $(PROTOCOL_CONTRACT_TEST)
	$(PROTOCOL_CONTRACT_TEST)

check-library-embed: $(PROTOCOL_FREE_EMBED_TEST)
	$(PROTOCOL_FREE_EMBED_TEST)

check-object-graphs: $(CORE_LIB) $(BOOT_LIB)
	$(PYTHON) tools/lint/object_graph_lint.py \
		--core $(CORE_LIB) \
		--boot $(BOOT_LIB)

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
	grep -q "session-state=3" $(BUILD_DIR)/host-reference.txt
	@echo "RIBON-R3-HOST-REFERENCE-OK $(RIBON_ARCH)"

check: legacy-hard-cut check-public-api check-loader check-rph1 \
	check-arch-x86_64 check-arch-aarch64 check-arch-ops \
	check-uefi-hardening check-core-service check-mode-descriptors \
	check-plugin-descriptors check-protocol-contract check-library-embed \
	check-object-graphs qstar-check
	@for arch in $(RIBON_ARCHES); do \
		$(MAKE) --no-print-directory RIBON_ARCH=$$arch check-one || exit $$?; \
	done
	@echo "RIBON-R3-AGGREGATE-OK"

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
