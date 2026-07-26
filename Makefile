ROOT := $(abspath .)
RIBON_ARCH ?= x86_64
RIBON_ARCHES := x86_64 aarch64 riscv64
RIBON_MODE ?= normal
RIBON_MODES := normal recovery provisioning diagnostic
BUILD_ROOT ?= $(ROOT)/build
ifeq ($(origin BUILD_DIR),undefined)
ifeq ($(RIBON_MODE),normal)
BUILD_DIR := $(BUILD_ROOT)/$(RIBON_ARCH)
else
BUILD_DIR := $(BUILD_ROOT)/modes/$(RIBON_MODE)/$(RIBON_ARCH)
endif
endif
CC ?= cc
AR ?= ar
PYTHON ?= python3
UEFI_CC ?= clang
UEFI_LD ?= lld-link
RPI_CC ?= clang
RPI_LD ?= ld.lld
OBJCOPY ?= llvm-objcopy
QSTAR ?= qstar
QEMU_X86_64 ?= qemu-system-x86_64
QEMU_AARCH64 ?= qemu-system-aarch64
SPHINX_BUILD ?= $(firstword $(wildcard $(BUILD_ROOT)/docs/venv/bin/sphinx-build) sphinx-build)
DOXYGEN ?= doxygen
QEMU_EDK2_X64_CODE ?= $(firstword $(wildcard /opt/homebrew/share/qemu/edk2-x86_64-code.fd /opt/homebrew/Cellar/qemu/*/share/qemu/edk2-x86_64-code.fd /usr/share/qemu/edk2-x86_64-code.fd /usr/share/OVMF/OVMF_CODE.fd))
QEMU_EDK2_AARCH64_CODE ?= $(firstword $(wildcard /opt/homebrew/share/qemu/edk2-aarch64-code.fd /opt/homebrew/Cellar/qemu/*/share/qemu/edk2-aarch64-code.fd /usr/share/qemu-efi-aarch64/QEMU_EFI.fd /usr/share/AAVMF/AAVMF_CODE.fd))

WARNFLAGS := -Wall -Wextra -Werror
CFLAGS ?= -std=c11 -O2 -g
CPPFLAGS += -I$(ROOT)/include

ifeq ($(filter $(RIBON_ARCH),$(RIBON_ARCHES)),)
$(error unsupported RIBON_ARCH=$(RIBON_ARCH); supported: $(RIBON_ARCHES))
endif
ifeq ($(filter $(RIBON_MODE),$(RIBON_MODES)),)
$(error unsupported RIBON_MODE=$(RIBON_MODE); supported: $(RIBON_MODES))
endif

LIB_NAME := libribon.a
LIB := $(BUILD_DIR)/$(LIB_NAME)
BOOTLOADER := $(BUILD_DIR)/ribon-boot
KERNEL_FIXTURE := $(BUILD_DIR)/fixtures/parus-kernel.elf
HIGHER_HALF_FIXTURE := $(BUILD_DIR)/fixtures/parus-higher-half.elf
DIRECT_HIGH_FIXTURE := $(BUILD_DIR)/fixtures/parus-direct-high.elf
PARUS_KERNEL_ELF ?= $(KERNEL_FIXTURE)
UEFI_APP := $(BUILD_DIR)/ribon-uefi.efi
UEFI_ESP_DIR := $(BUILD_DIR)/uefi-esp
UEFI_DIRECT_ESP_DIR := $(BUILD_DIR)/uefi-direct-esp
UEFI_PARUS_ESP_DIR := $(BUILD_DIR)/uefi-parus-esp
UEFI_PARUS_DIRECT_ESP_DIR := $(BUILD_DIR)/uefi-parus-direct-esp
UEFI_QEMU_LOG := $(BUILD_DIR)/qemu-uefi-smoke.log
UEFI_QEMU_AARCH64_LOG := $(BUILD_ROOT)/aarch64/qemu-uefi-aarch64-smoke.log
UEFI_QEMU_AARCH64_DIRECT_LOG := $(BUILD_ROOT)/aarch64/qemu-uefi-aarch64-direct-smoke.log
UEFI_QEMU_AARCH64_VARS := $(BUILD_ROOT)/aarch64/AAVMF_VARS.fd
UEFI_QEMU_PARUS_LOG := $(BUILD_DIR)/qemu-uefi-parus-smoke.log
UEFI_QEMU_PARUS_DIRECT_LOG := $(BUILD_DIR)/qemu-uefi-parus-direct-smoke.log
UEFI_QEMU_PARUS_RUNTIME_LOG := $(BUILD_DIR)/qemu-uefi-parus-runtime-smoke.log
UEFI_QEMU_PARUS_DIRECT_RUNTIME_LOG := $(BUILD_DIR)/qemu-uefi-parus-direct-runtime-smoke.log
UEFI_QEMU_PARUS_AARCH64_LOG := $(BUILD_ROOT)/aarch64/qemu-uefi-parus-aarch64-smoke.log
UEFI_QEMU_PARUS_AARCH64_DIRECT_LOG := $(BUILD_ROOT)/aarch64/qemu-uefi-parus-aarch64-direct-smoke.log
UEFI_QEMU_PARUS_AARCH64_RUNTIME_LOG := $(BUILD_ROOT)/aarch64/qemu-uefi-parus-aarch64-runtime-smoke.log
RPI_PROFILE ?= rpi5
RPI_BUILD_DIR := $(BUILD_ROOT)/rpi-$(RPI_PROFILE)
RPI_ELF := $(RPI_BUILD_DIR)/ribon-rpi5.elf
RPI_IMAGE := $(RPI_BUILD_DIR)/kernel8.img
RPI_PACKAGE_DIR := $(RPI_BUILD_DIR)/package
RPI_QEMU_LOG := $(RPI_BUILD_DIR)/qemu-rpi-smoke.log
RPI_PARUS_ELF := $(RPI_BUILD_DIR)/boot-media/kernel/kernel.elf
RPI_CMDLINE_TXT ?= $(ROOT)/configs/rpi5/cmdline.txt
RPI_CONFIG_TXT ?= $(ROOT)/configs/rpi5/config.txt
TEST_BUILD_DIR := $(BUILD_ROOT)/tests
LOADER_TEST := $(TEST_BUILD_DIR)/elf64_loader_tests
LOADER_TEST_SRCS := tests/loader/elf64_loader_tests.c src/loader/elf64.c
LOADER_TEST_OBJS := $(LOADER_TEST_SRCS:%.c=$(TEST_BUILD_DIR)/obj/%.o)
RPH1_TEST := $(TEST_BUILD_DIR)/rph1_builder_tests
RPH1_TEST_SRCS := tests/rph1/rph1_builder_tests.c src/profiles/parus/rph1_builder.c src/profiles/parus/rph1_parser.c src/core/memory.c
RPH1_TEST_OBJS := $(RPH1_TEST_SRCS:%.c=$(TEST_BUILD_DIR)/obj/%.o)
ARCH_X86_64_TEST := $(TEST_BUILD_DIR)/x86_64_direct_high_tests
ARCH_X86_64_TEST_SRCS := tests/arch/x86_64_direct_high_tests.c src/arch/common.c src/arch/x86_64/arch.c
ARCH_X86_64_TEST_OBJS := $(ARCH_X86_64_TEST_SRCS:%.c=$(TEST_BUILD_DIR)/obj/%.o)
ARCH_AARCH64_TEST := $(TEST_BUILD_DIR)/aarch64_direct_high_tests
ARCH_AARCH64_TEST_SRCS := tests/arch/aarch64_direct_high_tests.c src/arch/common.c src/arch/aarch64/arch.c
ARCH_AARCH64_TEST_OBJS := $(ARCH_AARCH64_TEST_SRCS:%.c=$(TEST_BUILD_DIR)/obj/%.o)
UEFI_HARDENING_TEST := $(TEST_BUILD_DIR)/uefi_hardening_tests
UEFI_HARDENING_TEST_SRCS := tests/uefi/uefi_hardening_tests.c src/firmware/uefi/hardening.c
UEFI_HARDENING_TEST_OBJS := $(UEFI_HARDENING_TEST_SRCS:%.c=$(TEST_BUILD_DIR)/obj/%.o)
CORE_SERVICE_TEST := $(TEST_BUILD_DIR)/core_service_boundary_tests
CORE_SERVICE_TEST_SRCS := \
	tests/core/service_boundary_tests.c \
	src/arch/common.c \
	src/arch/x86_64/arch.c \
	src/core/arena.c \
	src/core/context.c \
	src/core/memory.c \
	src/core/platform.c \
	src/core/profile.c \
	src/modes/normal.c \
	src/profiles/parus.c \
	src/profiles/parus/rph1_builder.c \
	src/profiles/parus/rph1_parser.c
CORE_SERVICE_TEST_OBJS := $(CORE_SERVICE_TEST_SRCS:%.c=$(TEST_BUILD_DIR)/obj/%.o)
MODE_GRAPH_BUILD_ROOT := $(BUILD_ROOT)/object-graphs
MODE_GRAPH_LINT := tools/lint/object_graph_lint.py
MODE_DESCRIPTOR_TESTS := $(RIBON_MODES:%=$(TEST_BUILD_DIR)/mode_descriptor_%_tests)
ARCH_OPS_TESTS := $(RIBON_ARCHES:%=$(TEST_BUILD_DIR)/arch_ops_%_tests)
QEMU_UEFI_SMOKE_TEST := tests/tools/qemu_uefi_smoke_tests.py
RPI_LOAD_ADDR ?= 0x00080000
RPI_RAM_BASE ?= 0x00000000
RPI_RAM_SIZE ?= 0x10000000
RPI_KERNEL_LOAD_BASE ?= 0x00200000
RPI5_UART_BASE ?= 0x107d001000
RPI_QEMU_UART_BASE ?= 0x09000000
RPI_UART_BASE ?= $(RPI5_UART_BASE)
RPI_BOARD_KIND ?= RIBON_RPI_BOARD_RPI5
RPI_ENABLE_KERNEL_JUMP ?= 0
UEFI_BINDING_DIR_x86_64 := X64
UEFI_BINDING_DIR_aarch64 := AArch64
UEFI_BOOT_FILE_x86_64 := BOOTX64.EFI
UEFI_BOOT_FILE_aarch64 := BOOTAA64.EFI
UEFI_TARGET_x86_64 := x86_64-unknown-windows
UEFI_TARGET_aarch64 := aarch64-unknown-windows
UEFI_SUPPORTED_ARCHES := x86_64 aarch64

FIRMWARE_SRCS := \
	src/firmware/common.c \
	src/firmware/host/host.c

MODE_SRC := src/modes/$(RIBON_MODE).c

LIB_SRCS := \
	src/arch/common.c \
	src/arch/$(RIBON_ARCH)/arch.c \
	$(FIRMWARE_SRCS) \
	src/loader/elf64.c \
	src/core/arena.c \
	src/core/context.c \
	src/core/memory.c \
	src/core/platform.c \
	src/core/profile.c \
	src/core/ribon.c \
	$(MODE_SRC) \
	src/profiles/parus.c \
	src/profiles/parus/rph1_builder.c \
	src/profiles/parus/rph1_parser.c

BOOT_SRCS := \
	src/boot/main.c

RPI_SRCS := \
	src/boot/rpi_entry.S \
	src/boot/rpi_main.c \
	src/boot/rpi_payload_embed.S \
	src/arch/common.c \
	src/arch/aarch64/arch.c \
	src/firmware/common.c \
	src/firmware/freestanding/string.c \
	src/firmware/rpi/rpi.c \
	src/loader/elf64.c \
	src/core/arena.c \
	src/core/context.c \
	src/core/memory.c \
	src/core/platform.c \
	src/core/profile.c \
	src/core/ribon.c \
	src/modes/normal.c \
	src/profiles/parus.c \
	src/profiles/parus/rph1_builder.c \
	src/profiles/parus/rph1_parser.c

UEFI_SRCS := \
	src/arch/common.c \
	src/arch/$(RIBON_ARCH)/arch.c \
	src/firmware/common.c \
	src/firmware/freestanding/string.c \
	src/firmware/uefi/hardening.c \
	src/loader/elf64.c \
	src/core/arena.c \
	src/core/context.c \
	src/core/memory.c \
	src/core/platform.c \
	src/core/profile.c \
	src/core/ribon.c \
	src/modes/normal.c \
	src/profiles/parus.c \
	src/profiles/parus/rph1_builder.c \
	src/profiles/parus/rph1_parser.c \
	src/boot/uefi_main.c

LIB_OBJS := $(LIB_SRCS:%.c=$(BUILD_DIR)/obj/%.o)
BOOT_OBJS := $(BOOT_SRCS:%.c=$(BUILD_DIR)/obj/%.o)
RPI_OBJS := $(RPI_SRCS:%=$(RPI_BUILD_DIR)/obj/%.o)
UEFI_OBJS := $(UEFI_SRCS:%.c=$(BUILD_DIR)/uefi-obj/%.obj)

UEFI_BINDING_DIR := $(UEFI_BINDING_DIR_$(RIBON_ARCH))
UEFI_BOOT_FILE := $(UEFI_BOOT_FILE_$(RIBON_ARCH))
UEFI_TARGET := $(UEFI_TARGET_$(RIBON_ARCH))
UEFI_CPPFLAGS := -I$(ROOT)/include/freestanding -I$(ROOT)/include/uefi/$(UEFI_BINDING_DIR) -I$(ROOT)/include/uefi -I$(ROOT)/include
UEFI_CFLAGS := -std=c11 -O2 -ffreestanding -fshort-wchar -fno-builtin -fno-stack-protector
UEFI_CFLAGS += -Wall -Wextra -Werror
ifeq ($(RIBON_ARCH),x86_64)
UEFI_CFLAGS += -mno-red-zone -mno-stack-arg-probe
endif
RPI_CPPFLAGS := -I$(ROOT)/include/freestanding -I$(ROOT)/include
RPI_CPPFLAGS += -DRIBON_RPI_UART_BASE=$(RPI_UART_BASE)
RPI_CPPFLAGS += -DRIBON_RPI_BOARD_KIND=$(RPI_BOARD_KIND)
RPI_CPPFLAGS += -DRIBON_RPI_PLATFORM_NAME=\"$(RPI_PROFILE)\"
RPI_CPPFLAGS += -DRIBON_RPI_KERNEL_ELF_PATH=\"$(RPI_PARUS_ELF)\"
RPI_CPPFLAGS += -DRIBON_RPI_CMDLINE_PATH=\"$(RPI_CMDLINE_TXT)\"
RPI_CPPFLAGS += -DRIBON_RPI_ENABLE_KERNEL_JUMP=$(RPI_ENABLE_KERNEL_JUMP)
RPI_CPPFLAGS += -DRIBON_RPI_RAM_BASE=$(RPI_RAM_BASE)
RPI_CPPFLAGS += -DRIBON_RPI_RAM_SIZE=$(RPI_RAM_SIZE)
RPI_CFLAGS := -target aarch64-none-elf -std=c11 -O2 -ffreestanding -fno-builtin
RPI_CFLAGS += -fno-stack-protector -fno-pic -mgeneral-regs-only -Wall -Wextra -Werror
RPI_ASFLAGS := -target aarch64-none-elf -ffreestanding -Wall -Wextra -Werror

.PHONY: all lib bootloader uefi-app rpi-image rpi5-image rpi-package rpi-package-one rpi-package-check check check-one check-loader check-rph1 check-arch-x86_64 check-arch-aarch64 check-arch-ops check-uefi-hardening check-core-service check-mode-descriptors check-object-graphs check-uefi-smoke-diagnostics check-uefi-build legacy-hard-cut qstar-list qstar-lint qstar-dry-run qstar-host-smoke qstar-check qemu-uefi-smoke qemu-uefi-aarch64-smoke qemu-uefi-aarch64-direct-smoke qemu-uefi-aarch64-direct-smoke-one qemu-uefi-parus-smoke qemu-uefi-parus-direct-smoke qemu-uefi-parus-runtime-smoke qemu-uefi-parus-direct-runtime-smoke qemu-uefi-parus-aarch64-smoke qemu-uefi-parus-aarch64-direct-smoke qemu-uefi-parus-aarch64-runtime-smoke qemu-rpi-smoke docs docs-lint docs-clean clean metadata

all: lib bootloader

lib: $(LIB)

bootloader: $(BOOTLOADER)

uefi-app: require-uefi-arch $(UEFI_APP)

docs: legacy-hard-cut
	$(PYTHON) tools/lint/documentation_quality_lint.py
	@mkdir -p $(BUILD_ROOT)/docs
	$(DOXYGEN) docs/Doxyfile
	$(SPHINX_BUILD) -W --keep-going -b html docs $(BUILD_ROOT)/docs/html

docs-lint: legacy-hard-cut
	$(PYTHON) tools/lint/documentation_quality_lint.py

legacy-hard-cut:
	$(PYTHON) tools/lint/legacy_os_hard_cut.py

docs-clean:
	rm -rf $(BUILD_ROOT)/docs

rpi-image: $(RPI_IMAGE)

rpi5-image:
	$(MAKE) --no-print-directory RPI_PROFILE=rpi5 RPI_UART_BASE=$(RPI5_UART_BASE) RPI_BOARD_KIND=RIBON_RPI_BOARD_RPI5 rpi-image

rpi-package:
	$(MAKE) --no-print-directory RPI_PROFILE=rpi5 RPI_UART_BASE=$(RPI5_UART_BASE) RPI_BOARD_KIND=RIBON_RPI_BOARD_RPI5 rpi-package-one
	$(PYTHON) tools/check_rpi_package.py --package $(BUILD_ROOT)/rpi-rpi5/package

$(BUILD_DIR)/obj/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) -c $< -o $@

$(TEST_BUILD_DIR)/obj/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) -c $< -o $@

$(RPI_BUILD_DIR)/obj/%.c.o: %.c
	@mkdir -p $(@D)
	$(RPI_CC) $(RPI_CPPFLAGS) $(RPI_CFLAGS) -c $< -o $@

$(RPI_BUILD_DIR)/obj/%.S.o: %.S
	@mkdir -p $(@D)
	$(RPI_CC) $(RPI_CPPFLAGS) $(RPI_ASFLAGS) -c $< -o $@

$(RPI_BUILD_DIR)/obj/src/boot/rpi_payload_embed.S.o: $(RPI_PARUS_ELF) $(RPI_CMDLINE_TXT)

$(BUILD_DIR)/uefi-obj/%.obj: %.c
	@mkdir -p $(@D)
	$(UEFI_CC) -target $(UEFI_TARGET) $(UEFI_CPPFLAGS) $(UEFI_CFLAGS) -c $< -o $@

$(LIB): $(LIB_OBJS) Makefile
	@mkdir -p $(@D)
	$(RM) $@
	$(AR) rcs $@ $(LIB_OBJS)

$(BOOTLOADER): $(BOOT_OBJS) $(LIB)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(BOOT_OBJS) $(LIB) -o $@

$(LOADER_TEST): $(LOADER_TEST_OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(RPH1_TEST): $(RPH1_TEST_OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(ARCH_X86_64_TEST): $(ARCH_X86_64_TEST_OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(ARCH_AARCH64_TEST): $(ARCH_AARCH64_TEST_OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(UEFI_HARDENING_TEST): $(UEFI_HARDENING_TEST_OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(CORE_SERVICE_TEST): $(CORE_SERVICE_TEST_OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(TEST_BUILD_DIR)/mode_descriptor_%_tests: tests/core/mode_descriptor_tests.c src/modes/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(TEST_BUILD_DIR)/arch_ops_%_tests: tests/arch/arch_ops_tests.c src/arch/common.c src/arch/%/arch.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(UEFI_APP): require-uefi-arch $(UEFI_OBJS)
	@mkdir -p $(@D)
	$(UEFI_LD) /subsystem:efi_application /entry:efi_main /nodefaultlib /out:$@ $(UEFI_OBJS)

$(RPI_ELF): $(RPI_OBJS) linker/rpi5-aarch64.ld
	@mkdir -p $(@D)
	$(RPI_LD) -nostdlib -T linker/rpi5-aarch64.ld --defsym=RIBON_RPI_LOAD_ADDRESS=$(RPI_LOAD_ADDR) -o $@ $(RPI_OBJS)

$(RPI_IMAGE): $(RPI_ELF)
	$(OBJCOPY) -O binary $< $@

$(RPI_PARUS_ELF): tools/make_elf64_fixture.py
	@mkdir -p $(@D)
	$(PYTHON) $< --arch aarch64 --base $(RPI_KERNEL_LOAD_BASE) --output $@

$(KERNEL_FIXTURE): tools/make_elf64_fixture.py
	@mkdir -p $(@D)
	$(PYTHON) $< --arch $(RIBON_ARCH) --output $@

$(HIGHER_HALF_FIXTURE): tools/make_elf64_fixture.py
	@mkdir -p $(@D)
	$(PYTHON) $< --arch $(RIBON_ARCH) --layout higher-half --base 0x200000 --high-base 0xffffffff80000000 --output $@

$(DIRECT_HIGH_FIXTURE): tools/make_elf64_fixture.py
	@mkdir -p $(@D)
	$(PYTHON) $< --arch $(RIBON_ARCH) --layout higher-half --base 0x200000 --high-base 0xffffffff80000000 --entry-at-base --expected-entry-flags 0xd --output $@

require-uefi-arch:
	@if [ -z "$(UEFI_TARGET)" ]; then \
		echo "unsupported UEFI RIBON_ARCH=$(RIBON_ARCH); supported: $(UEFI_SUPPORTED_ARCHES)" >&2; \
		exit 2; \
	fi

check: legacy-hard-cut check-loader check-rph1 check-arch-x86_64 check-arch-aarch64 check-arch-ops check-uefi-hardening check-core-service check-mode-descriptors check-object-graphs check-uefi-smoke-diagnostics
	@for arch in $(RIBON_ARCHES); do \
		echo "RIBON-ARCH-CHECK $$arch"; \
		$(MAKE) --no-print-directory RIBON_ARCH=$$arch check-one || exit $$?; \
	done
	@echo "RIBON-ARCH-MATRIX-OK"

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

check-object-graphs:
	@for mode in $(RIBON_MODES); do \
		$(MAKE) --no-print-directory \
			RIBON_ARCH=x86_64 \
			RIBON_MODE=$$mode \
			BUILD_DIR=$(MODE_GRAPH_BUILD_ROOT)/$$mode \
			lib || exit $$?; \
	done
	$(PYTHON) $(MODE_GRAPH_LINT) \
		--archive normal=$(MODE_GRAPH_BUILD_ROOT)/normal/$(LIB_NAME) \
		--archive recovery=$(MODE_GRAPH_BUILD_ROOT)/recovery/$(LIB_NAME) \
		--archive provisioning=$(MODE_GRAPH_BUILD_ROOT)/provisioning/$(LIB_NAME) \
		--archive diagnostic=$(MODE_GRAPH_BUILD_ROOT)/diagnostic/$(LIB_NAME)

check-uefi-smoke-diagnostics:
	$(PYTHON) $(QEMU_UEFI_SMOKE_TEST)

check-one: all $(KERNEL_FIXTURE) $(HIGHER_HALF_FIXTURE)
	$(BOOTLOADER) --profile parus --kernel $(KERNEL_FIXTURE) > $(BUILD_DIR)/parus-profile.txt
	grep -q "arch=$(RIBON_ARCH)" $(BUILD_DIR)/parus-profile.txt
	grep -q "page-size=4096" $(BUILD_DIR)/parus-profile.txt
	grep -q "kernel-alignment=2097152" $(BUILD_DIR)/parus-profile.txt
	grep -q "environment-flags=0x00000039" $(BUILD_DIR)/parus-profile.txt
	grep -q "memory-regions=6" $(BUILD_DIR)/parus-profile.txt
	grep -q "normalized-memory-regions=5" $(BUILD_DIR)/parus-profile.txt
	grep -q "usable-memory=0x000000003fd00000" $(BUILD_DIR)/parus-profile.txt
	grep -q "boot-media=file" $(BUILD_DIR)/parus-profile.txt
	grep -q "boot-modules=1" $(BUILD_DIR)/parus-profile.txt
	grep -q "profile=parus" $(BUILD_DIR)/parus-profile.txt
	grep -q "kernel=kernel/kernel.elf" $(BUILD_DIR)/parus-profile.txt
	grep -q "handoff=rph1" $(BUILD_DIR)/parus-profile.txt
	grep -q "handoff-kind=profile-defined" $(BUILD_DIR)/parus-profile.txt
	grep -q "handoff-artifact-format=rph1" $(BUILD_DIR)/parus-profile.txt
	grep -Eq "handoff-artifact-size=[1-9][0-9]*" $(BUILD_DIR)/parus-profile.txt
	grep -q "handoff-artifact-sections=7" $(BUILD_DIR)/parus-profile.txt
	grep -q "payload-format=elf64" $(BUILD_DIR)/parus-profile.txt
	grep -q "kernel-load-segments=1" $(BUILD_DIR)/parus-profile.txt
	grep -q "kernel-load-plan-flags=0x00000027" $(BUILD_DIR)/parus-profile.txt
	grep -q "kernel-entry=0x0000000000200078" $(BUILD_DIR)/parus-profile.txt
	grep -q "kernel-entry-load=0x0000000000200078" $(BUILD_DIR)/parus-profile.txt
	grep -q "kernel-runtime-entry=0x0000000000200078" $(BUILD_DIR)/parus-profile.txt
	grep -q "kernel-load-base=0x0000000000200000" $(BUILD_DIR)/parus-profile.txt
	grep -q "kernel-load-end=0x0000000000201000" $(BUILD_DIR)/parus-profile.txt
	grep -q "kernel-runtime-load-base=0x0000000000200000" $(BUILD_DIR)/parus-profile.txt
	grep -q "kernel-runtime-load-end=0x0000000000201000" $(BUILD_DIR)/parus-profile.txt
	grep -q "kernel-memory-size=0x0000000000001000" $(BUILD_DIR)/parus-profile.txt
	grep -q "kernel-linked-vaddr-base=0x0000000000200000" $(BUILD_DIR)/parus-profile.txt
	grep -q "kernel-linked-vaddr-end=0x0000000000201000" $(BUILD_DIR)/parus-profile.txt
	grep -q "kernel-linked-paddr-base=0x0000000000200000" $(BUILD_DIR)/parus-profile.txt
	grep -q "kernel-linked-paddr-end=0x0000000000201000" $(BUILD_DIR)/parus-profile.txt
	grep -q "kernel-high-entry=0x0000000000000000" $(BUILD_DIR)/parus-profile.txt
	grep -q "kernel-high-entry-load=0x0000000000000000" $(BUILD_DIR)/parus-profile.txt
	$(BOOTLOADER) --profile parus --kernel $(HIGHER_HALF_FIXTURE) > $(BUILD_DIR)/parus-higher-half-profile.txt
	grep -q "arch=$(RIBON_ARCH)" $(BUILD_DIR)/parus-higher-half-profile.txt
	grep -q "payload-format=elf64" $(BUILD_DIR)/parus-higher-half-profile.txt
	grep -Eq "handoff-artifact-size=[1-9][0-9]*" $(BUILD_DIR)/parus-higher-half-profile.txt
	grep -q "handoff-artifact-sections=7" $(BUILD_DIR)/parus-higher-half-profile.txt
	grep -q "kernel-load-segments=1" $(BUILD_DIR)/parus-higher-half-profile.txt
	grep -q "kernel-load-plan-flags=0x0000003f" $(BUILD_DIR)/parus-higher-half-profile.txt
	grep -q "kernel-entry=0xffffffff80000078" $(BUILD_DIR)/parus-higher-half-profile.txt
	grep -q "kernel-entry-load=0x0000000000200078" $(BUILD_DIR)/parus-higher-half-profile.txt
	grep -q "kernel-runtime-entry=0x0000000000200078" $(BUILD_DIR)/parus-higher-half-profile.txt
	grep -q "kernel-load-base=0x0000000000200000" $(BUILD_DIR)/parus-higher-half-profile.txt
	grep -q "kernel-load-end=0x0000000000201000" $(BUILD_DIR)/parus-higher-half-profile.txt
	grep -q "kernel-runtime-load-base=0x0000000000200000" $(BUILD_DIR)/parus-higher-half-profile.txt
	grep -q "kernel-runtime-load-end=0x0000000000201000" $(BUILD_DIR)/parus-higher-half-profile.txt
	grep -q "kernel-memory-size=0x0000000000001000" $(BUILD_DIR)/parus-higher-half-profile.txt
	grep -q "kernel-linked-vaddr-base=0xffffffff80000000" $(BUILD_DIR)/parus-higher-half-profile.txt
	grep -q "kernel-linked-vaddr-end=0xffffffff80001000" $(BUILD_DIR)/parus-higher-half-profile.txt
	grep -q "kernel-linked-paddr-base=0x0000000000200000" $(BUILD_DIR)/parus-higher-half-profile.txt
	grep -q "kernel-linked-paddr-end=0x0000000000201000" $(BUILD_DIR)/parus-higher-half-profile.txt
	grep -q "kernel-high-entry=0xffffffff80000000" $(BUILD_DIR)/parus-higher-half-profile.txt
	grep -q "kernel-high-entry-load=0x0000000000200000" $(BUILD_DIR)/parus-higher-half-profile.txt
	@echo "RIBON-CORE-CHECK-OK"

check-uefi-build: uefi-app
	@echo "RIBON-UEFI-BUILD-OK"

qstar-list:
	$(QSTAR) --file qstar.lua list-targets

qstar-lint:
	$(QSTAR) --file qstar.lua check
	$(QSTAR) --file qstar.lua lint //src/boot:ribon_boot_x86_64

qstar-dry-run:
	$(QSTAR) --file qstar.lua dry-run //src/boot:ribon_boot_x86_64
	$(QSTAR) --file qstar.lua dry-run //src/boot:ribon_uefi_x64
	$(QSTAR) --file qstar.lua dry-run //src/boot:ribon_uefi_aa64
	$(QSTAR) --file qstar.lua dry-run //src/boot:ribon_rpi_payload_rpi5
	$(QSTAR) --file qstar.lua dry-run //src/boot:ribon_rpi_payload_qemu_virt

qstar-host-smoke:
	$(QSTAR) --file qstar.lua test //...
	$(QSTAR) --file qstar.lua build //src/core:ribon_non_normal_mode_cores
	$(QSTAR) --file qstar.lua build //src/boot:host_smoke_x86_64
	$(QSTAR) --file qstar.lua build //src/boot:host_smoke_aarch64
	$(QSTAR) --file qstar.lua build //src/boot:host_smoke_riscv64

qstar-check: legacy-hard-cut qstar-lint qstar-dry-run qstar-host-smoke
	@echo "RIBON-QSTAR-CHECK-OK"

$(UEFI_ESP_DIR)/EFI/BOOT/$(UEFI_BOOT_FILE): $(UEFI_APP)
	@mkdir -p $(@D)
	cp $(UEFI_APP) $@

$(UEFI_ESP_DIR)/kernel/kernel.elf: $(KERNEL_FIXTURE)
	@mkdir -p $(@D)
	cp $(KERNEL_FIXTURE) $@

$(UEFI_DIRECT_ESP_DIR)/EFI/BOOT/$(UEFI_BOOT_FILE): $(UEFI_APP)
	@mkdir -p $(@D)
	cp $(UEFI_APP) $@

$(UEFI_DIRECT_ESP_DIR)/kernel/kernel.elf: $(DIRECT_HIGH_FIXTURE)
	@mkdir -p $(@D)
	cp $(DIRECT_HIGH_FIXTURE) $@

$(UEFI_DIRECT_ESP_DIR)/ribon-direct-high:
	@mkdir -p $(@D)
	touch $@

$(UEFI_PARUS_ESP_DIR)/EFI/BOOT/$(UEFI_BOOT_FILE): $(UEFI_APP)
	@mkdir -p $(@D)
	cp $(UEFI_APP) $@

$(UEFI_PARUS_ESP_DIR)/kernel/kernel.elf: $(PARUS_KERNEL_ELF)
	@mkdir -p $(@D)
	cp $(PARUS_KERNEL_ELF) $@

$(UEFI_PARUS_DIRECT_ESP_DIR)/EFI/BOOT/$(UEFI_BOOT_FILE): $(UEFI_APP)
	@mkdir -p $(@D)
	cp $(UEFI_APP) $@

$(UEFI_PARUS_DIRECT_ESP_DIR)/kernel/kernel.elf: $(PARUS_KERNEL_ELF)
	@mkdir -p $(@D)
	cp $(PARUS_KERNEL_ELF) $@

$(UEFI_PARUS_DIRECT_ESP_DIR)/ribon-direct-high:
	@mkdir -p $(@D)
	touch $@

$(UEFI_QEMU_AARCH64_VARS):
	@mkdir -p $(@D)
	dd if=/dev/zero of=$@ bs=1m count=64 status=none

qemu-uefi-smoke: require-uefi-arch $(UEFI_ESP_DIR)/EFI/BOOT/$(UEFI_BOOT_FILE) $(UEFI_ESP_DIR)/kernel/kernel.elf $(if $(filter aarch64,$(RIBON_ARCH)),$(UEFI_QEMU_AARCH64_VARS))
	@if [ "$(RIBON_ARCH)" = "x86_64" ]; then \
		$(PYTHON) tools/qemu_uefi_smoke.py \
		    --arch x86_64 \
		    --qemu $(QEMU_X86_64) \
		    --firmware "$(QEMU_EDK2_X64_CODE)" \
		    --esp $(UEFI_ESP_DIR) \
		    --log $(UEFI_QEMU_LOG); \
	elif [ "$(RIBON_ARCH)" = "aarch64" ]; then \
		$(PYTHON) tools/qemu_uefi_smoke.py \
		    --arch aarch64 \
		    --qemu $(QEMU_AARCH64) \
		    --firmware "$(QEMU_EDK2_AARCH64_CODE)" \
		    --vars "$(UEFI_QEMU_AARCH64_VARS)" \
		    --esp $(UEFI_ESP_DIR) \
		    --log $(UEFI_QEMU_AARCH64_LOG) \
		    --machine virt,gic-version=2 \
		    --cpu cortex-a72 \
		    --memory 512M \
		    --timeout 30; \
	else \
		echo "QEMU UEFI smoke is unsupported for RIBON_ARCH=$(RIBON_ARCH)" >&2; \
		exit 2; \
	fi

qemu-uefi-aarch64-smoke:
	$(MAKE) --no-print-directory RIBON_ARCH=aarch64 qemu-uefi-smoke

qemu-uefi-aarch64-direct-smoke:
	$(MAKE) --no-print-directory RIBON_ARCH=aarch64 qemu-uefi-aarch64-direct-smoke-one

qemu-uefi-aarch64-direct-smoke-one: require-uefi-arch $(UEFI_DIRECT_ESP_DIR)/EFI/BOOT/$(UEFI_BOOT_FILE) $(UEFI_DIRECT_ESP_DIR)/kernel/kernel.elf $(UEFI_DIRECT_ESP_DIR)/ribon-direct-high $(UEFI_QEMU_AARCH64_VARS)
	@if [ "$(RIBON_ARCH)" != "aarch64" ]; then \
		echo "AArch64 direct UEFI smoke requires RIBON_ARCH=aarch64" >&2; \
		exit 2; \
	fi
	$(PYTHON) tools/qemu_uefi_smoke.py \
	    --arch aarch64 \
	    --qemu $(QEMU_AARCH64) \
	    --firmware "$(QEMU_EDK2_AARCH64_CODE)" \
	    --vars "$(UEFI_QEMU_AARCH64_VARS)" \
	    --esp $(UEFI_DIRECT_ESP_DIR) \
	    --log $(UEFI_QEMU_AARCH64_DIRECT_LOG) \
	    --machine virt,gic-version=2 \
	    --cpu cortex-a72 \
	    --memory 512M \
	    --timeout 30 \
	    --marker PARUS-FIXTURE-ENTRY-OK \
	    --require-marker RIBON-UEFI-KERNEL-LOAD-START \
	    --require-marker RIBON-UEFI-KERNEL-SEGMENT-RUNTIME= \
	    --require-marker RIBON-UEFI-KERNEL-RUNTIME-ENTRY= \
	    --require-marker RIBON-UEFI-KERNEL-LOAD-OK \
	    --require-marker RIBON-UEFI-DIRECT-HIGH-REQUESTED \
	    --require-marker RIBON-UEFI-DIRECT-HIGH-PREPARED \
	    --require-marker RIBON-UEFI-FINAL-MEMORY-MAP \
	    --require-marker RIBON-UEFI-FINAL-MEMORY-MAP-DESCRIPTORS= \
	    --require-marker RIBON-UEFI-RPH1-SIZE= \
	    --require-marker RIBON-UEFI-RPH1-SECTIONS= \
	    --require-marker RIBON-UEFI-RPH1-REBUILD-ATTEMPT= \
	    --require-marker RIBON-UEFI-JUMP-ENTRY= \
	    --require-marker RIBON-UEFI-JUMP-FLAGS=0x000000000000000d \
	    --require-marker RIBON-UEFI-HANDOFF= \
	    --require-marker RIBON-UEFI-DIAG-STAGE=exit-boot-services \
	    --require-marker RIBON-UEFI-EXIT-BOOT-SERVICES-START \
	    --require-marker PARUS-FIXTURE-ENTRY-OK

qemu-uefi-parus-smoke: require-uefi-arch $(UEFI_PARUS_ESP_DIR)/EFI/BOOT/$(UEFI_BOOT_FILE) $(UEFI_PARUS_ESP_DIR)/kernel/kernel.elf
	@if [ "$(RIBON_ARCH)" != "x86_64" ]; then \
		echo "Parus UEFI smoke is currently wired for x86_64; use check-uefi-build for $(RIBON_ARCH)" >&2; \
		exit 2; \
	fi
	$(PYTHON) tools/qemu_uefi_smoke.py \
	    --qemu $(QEMU_X86_64) \
	    --firmware "$(QEMU_EDK2_X64_CODE)" \
	    --esp $(UEFI_PARUS_ESP_DIR) \
	    --log $(UEFI_QEMU_PARUS_LOG) \
	    --timeout 30 \
	    --marker PARUS-HIGHER-HALF-CONTRACT-OK \
	    --require-marker RIBON-UEFI-KERNEL-LOAD-START \
	    --require-marker RIBON-UEFI-KERNEL-LOAD-OK \
	    --require-marker RIBON-UEFI-FINAL-MEMORY-MAP \
	    --require-marker RIBON-UEFI-FINAL-MEMORY-MAP-DESCRIPTORS= \
	    --require-marker RIBON-UEFI-RPH1-SIZE= \
	    --require-marker RIBON-UEFI-RPH1-REBUILD-ATTEMPT= \
	    --require-marker RIBON-UEFI-JUMP-ENTRY= \
	    --require-marker RIBON-UEFI-DIAG-STAGE=exit-boot-services \
	    --require-marker RIBON-UEFI-EXIT-BOOT-SERVICES-START \
	    --require-marker PARUS-LOW-TRAMPOLINE-OK \
	    --require-marker PARUS-HIGH-ENTRY-OK \
	    --require-marker PARUS-HIGH-ENTRY-ABI-OK \
	    --require-marker PARUS-LOW-TRAMPOLINE-FALLBACK-OK \
	    --require-marker XIBALBA-OK \
	    --require-marker PARUS-KERNEL-IMAGE-LAYOUT-OK \
	    --require-marker PARUS-HIGHER-HALF-CONTRACT-OK

qemu-uefi-parus-direct-smoke: require-uefi-arch $(UEFI_PARUS_DIRECT_ESP_DIR)/EFI/BOOT/$(UEFI_BOOT_FILE) $(UEFI_PARUS_DIRECT_ESP_DIR)/kernel/kernel.elf $(UEFI_PARUS_DIRECT_ESP_DIR)/ribon-direct-high
	@if [ "$(RIBON_ARCH)" != "x86_64" ]; then \
		echo "Parus direct-high UEFI smoke is currently wired for x86_64; use check-uefi-build for $(RIBON_ARCH)" >&2; \
		exit 2; \
	fi
	$(PYTHON) tools/qemu_uefi_smoke.py \
	    --qemu $(QEMU_X86_64) \
	    --firmware "$(QEMU_EDK2_X64_CODE)" \
	    --esp $(UEFI_PARUS_DIRECT_ESP_DIR) \
	    --log $(UEFI_QEMU_PARUS_DIRECT_LOG) \
	    --timeout 30 \
	    --marker PARUS-HIGHER-HALF-CONTRACT-OK \
	    --require-marker RIBON-UEFI-KERNEL-LOAD-START \
	    --require-marker RIBON-UEFI-KERNEL-LOAD-OK \
	    --require-marker RIBON-UEFI-DIRECT-HIGH-REQUESTED \
	    --require-marker RIBON-UEFI-DIRECT-HIGH-PREPARED \
	    --require-marker RIBON-UEFI-FINAL-MEMORY-MAP \
	    --require-marker RIBON-UEFI-FINAL-MEMORY-MAP-DESCRIPTORS= \
	    --require-marker RIBON-UEFI-RPH1-SIZE= \
	    --require-marker RIBON-UEFI-RPH1-REBUILD-ATTEMPT= \
	    --require-marker RIBON-UEFI-JUMP-FLAGS=0x000000000000000d \
	    --require-marker RIBON-UEFI-DIAG-STAGE=exit-boot-services \
	    --require-marker RIBON-UEFI-EXIT-BOOT-SERVICES-START \
	    --require-marker PARUS-HIGH-ENTRY-OK \
	    --require-marker PARUS-HIGH-ENTRY-ABI-OK \
	    --require-marker PARUS-RIBON-DIRECT-HIGH-OK \
	    --require-marker XIBALBA-OK \
	    --require-marker PARUS-KERNEL-IMAGE-LAYOUT-OK \
	    --require-marker PARUS-HIGHER-HALF-CONTRACT-OK \
	    --fail-marker PARUS-LOW-TRAMPOLINE-FALLBACK-OK

qemu-uefi-parus-runtime-smoke: require-uefi-arch $(UEFI_PARUS_ESP_DIR)/EFI/BOOT/$(UEFI_BOOT_FILE) $(UEFI_PARUS_ESP_DIR)/kernel/kernel.elf
	@if [ "$(RIBON_ARCH)" != "x86_64" ]; then \
		echo "Parus full runtime UEFI smoke is currently wired for x86_64; use check-uefi-build for $(RIBON_ARCH)" >&2; \
		exit 2; \
	fi
	$(PYTHON) tools/qemu_uefi_smoke.py \
	    --qemu $(QEMU_X86_64) \
	    --firmware "$(QEMU_EDK2_X64_CODE)" \
	    --esp $(UEFI_PARUS_ESP_DIR) \
	    --log $(UEFI_QEMU_PARUS_RUNTIME_LOG) \
	    --memory 512M \
	    --timeout 60 \
	    --marker PARUS-KCONSOLE-OK \
	    --require-marker RIBON-UEFI-KERNEL-LOAD-START \
	    --require-marker RIBON-UEFI-KERNEL-LOAD-OK \
	    --require-marker RIBON-UEFI-FINAL-MEMORY-MAP \
	    --require-marker RIBON-UEFI-FINAL-MEMORY-MAP-DESCRIPTORS= \
	    --require-marker RIBON-UEFI-RPH1-SIZE= \
	    --require-marker RIBON-UEFI-RPH1-REBUILD-ATTEMPT= \
	    --require-marker RIBON-UEFI-JUMP-ENTRY= \
	    --require-marker RIBON-UEFI-DIAG-STAGE=exit-boot-services \
	    --require-marker RIBON-UEFI-EXIT-BOOT-SERVICES-START \
	    --require-marker PARUS-LOW-TRAMPOLINE-FALLBACK-OK \
	    --require-marker XIBALBA-OK \
	    --require-marker PARUS-HIGHER-HALF-CONTRACT-OK \
	    --require-marker EARLY_BOOT-OK \
	    --require-marker PARUS-POSTBOOT-LOG-OK \
	    --require-marker PARUS-RUNTIME-IDLE \
	    --require-marker PARUS-KCONSOLE-OK

qemu-uefi-parus-direct-runtime-smoke: require-uefi-arch $(UEFI_PARUS_DIRECT_ESP_DIR)/EFI/BOOT/$(UEFI_BOOT_FILE) $(UEFI_PARUS_DIRECT_ESP_DIR)/kernel/kernel.elf $(UEFI_PARUS_DIRECT_ESP_DIR)/ribon-direct-high
	@if [ "$(RIBON_ARCH)" != "x86_64" ]; then \
		echo "Parus direct-high full runtime UEFI smoke is currently wired for x86_64; use check-uefi-build for $(RIBON_ARCH)" >&2; \
		exit 2; \
	fi
	$(PYTHON) tools/qemu_uefi_smoke.py \
	    --qemu $(QEMU_X86_64) \
	    --firmware "$(QEMU_EDK2_X64_CODE)" \
	    --esp $(UEFI_PARUS_DIRECT_ESP_DIR) \
	    --log $(UEFI_QEMU_PARUS_DIRECT_RUNTIME_LOG) \
	    --memory 512M \
	    --timeout 60 \
	    --marker PARUS-KCONSOLE-OK \
	    --require-marker RIBON-UEFI-KERNEL-LOAD-START \
	    --require-marker RIBON-UEFI-KERNEL-LOAD-OK \
	    --require-marker RIBON-UEFI-DIRECT-HIGH-REQUESTED \
	    --require-marker RIBON-UEFI-DIRECT-HIGH-PREPARED \
	    --require-marker RIBON-UEFI-FINAL-MEMORY-MAP \
	    --require-marker RIBON-UEFI-FINAL-MEMORY-MAP-DESCRIPTORS= \
	    --require-marker RIBON-UEFI-RPH1-SIZE= \
	    --require-marker RIBON-UEFI-RPH1-REBUILD-ATTEMPT= \
	    --require-marker RIBON-UEFI-JUMP-FLAGS=0x000000000000000d \
	    --require-marker RIBON-UEFI-DIAG-STAGE=exit-boot-services \
	    --require-marker RIBON-UEFI-EXIT-BOOT-SERVICES-START \
	    --require-marker PARUS-RIBON-DIRECT-HIGH-OK \
	    --require-marker XIBALBA-OK \
	    --require-marker PARUS-HIGHER-HALF-CONTRACT-OK \
	    --require-marker EARLY_BOOT-OK \
	    --require-marker PARUS-POSTBOOT-LOG-OK \
	    --require-marker PARUS-RUNTIME-IDLE \
	    --require-marker PARUS-KCONSOLE-OK \
	    --fail-marker PARUS-LOW-TRAMPOLINE-FALLBACK-OK

qemu-uefi-parus-aarch64-smoke: require-uefi-arch $(UEFI_PARUS_ESP_DIR)/EFI/BOOT/$(UEFI_BOOT_FILE) $(UEFI_PARUS_ESP_DIR)/kernel/kernel.elf $(UEFI_QEMU_AARCH64_VARS)
	@if [ "$(RIBON_ARCH)" != "aarch64" ]; then \
		echo "AArch64 Parus UEFI smoke requires RIBON_ARCH=aarch64" >&2; \
		exit 2; \
	fi
	$(PYTHON) tools/qemu_uefi_smoke.py \
	    --arch aarch64 \
	    --qemu $(QEMU_AARCH64) \
	    --firmware "$(QEMU_EDK2_AARCH64_CODE)" \
	    --vars "$(UEFI_QEMU_AARCH64_VARS)" \
	    --esp $(UEFI_PARUS_ESP_DIR) \
	    --log $(UEFI_QEMU_PARUS_AARCH64_LOG) \
	    --machine virt,gic-version=2 \
	    --cpu cortex-a72 \
	    --memory 1024M \
	    --timeout 60 \
	    --marker EARLY_BOOT-OK \
	    --require-marker RIBON-UEFI-KERNEL-LOAD-START \
	    --require-marker RIBON-UEFI-KERNEL-SEGMENT-RUNTIME= \
	    --require-marker RIBON-UEFI-KERNEL-RUNTIME-ENTRY= \
	    --require-marker RIBON-UEFI-KERNEL-LOAD-OK \
	    --require-marker RIBON-UEFI-FINAL-MEMORY-MAP \
	    --require-marker RIBON-UEFI-FINAL-MEMORY-MAP-DESCRIPTORS= \
	    --require-marker RIBON-UEFI-RPH1-SIZE= \
	    --require-marker RIBON-UEFI-RPH1-SECTIONS= \
	    --require-marker RIBON-UEFI-RPH1-REBUILD-ATTEMPT= \
	    --require-marker RIBON-UEFI-JUMP-ENTRY= \
	    --require-marker RIBON-UEFI-JUMP-FLAGS=0x0000000000000001 \
	    --require-marker RIBON-UEFI-HANDOFF= \
	    --require-marker RIBON-UEFI-DIAG-STAGE=exit-boot-services \
	    --require-marker RIBON-UEFI-EXIT-BOOT-SERVICES-START \
	    --require-marker PARUS-HIGH-ENTRY-OK \
	    --require-marker PARUS-HIGH-ENTRY-ABI-OK \
	    --require-marker XIBALBA-OK \
	    --require-marker PARUS-KERNEL-IMAGE-LAYOUT-OK \
	    --require-marker PARUS-HIGHER-HALF-CONTRACT-OK \
	    --require-marker EARLY_BOOT-OK

qemu-uefi-parus-aarch64-direct-smoke: require-uefi-arch $(UEFI_PARUS_DIRECT_ESP_DIR)/EFI/BOOT/$(UEFI_BOOT_FILE) $(UEFI_PARUS_DIRECT_ESP_DIR)/kernel/kernel.elf $(UEFI_PARUS_DIRECT_ESP_DIR)/ribon-direct-high $(UEFI_QEMU_AARCH64_VARS)
	@if [ "$(RIBON_ARCH)" != "aarch64" ]; then \
		echo "AArch64 Parus direct UEFI smoke requires RIBON_ARCH=aarch64" >&2; \
		exit 2; \
	fi
	$(PYTHON) tools/qemu_uefi_smoke.py \
	    --arch aarch64 \
	    --qemu $(QEMU_AARCH64) \
	    --firmware "$(QEMU_EDK2_AARCH64_CODE)" \
	    --vars "$(UEFI_QEMU_AARCH64_VARS)" \
	    --esp $(UEFI_PARUS_DIRECT_ESP_DIR) \
	    --log $(UEFI_QEMU_PARUS_AARCH64_DIRECT_LOG) \
	    --machine virt,gic-version=2 \
	    --cpu cortex-a72 \
	    --memory 1024M \
	    --storage-media null-co \
	    --timeout 75 \
	    --marker PARUS-AARCH64-BRINGUP-OK \
	    --require-marker RIBON-UEFI-KERNEL-LOAD-START \
	    --require-marker RIBON-UEFI-KERNEL-SEGMENT-RUNTIME= \
	    --require-marker RIBON-UEFI-KERNEL-RUNTIME-ENTRY= \
	    --require-marker RIBON-UEFI-KERNEL-LOAD-OK \
	    --require-marker RIBON-UEFI-DIRECT-HIGH-REQUESTED \
	    --require-marker RIBON-UEFI-DIRECT-HIGH-PREPARED \
	    --require-marker RIBON-UEFI-FINAL-MEMORY-MAP \
	    --require-marker RIBON-UEFI-FINAL-MEMORY-MAP-DESCRIPTORS= \
	    --require-marker RIBON-UEFI-RPH1-SIZE= \
	    --require-marker RIBON-UEFI-RPH1-SECTIONS= \
	    --require-marker RIBON-UEFI-RPH1-REBUILD-ATTEMPT= \
	    --require-marker RIBON-UEFI-JUMP-ENTRY= \
	    --require-marker RIBON-UEFI-JUMP-FLAGS=0x000000000000000d \
	    --require-marker RIBON-UEFI-HANDOFF= \
	    --require-marker RIBON-UEFI-DIAG-STAGE=exit-boot-services \
	    --require-marker RIBON-UEFI-EXIT-BOOT-SERVICES-START \
	    --require-marker PARUS-HIGH-ENTRY-OK \
	    --require-marker PARUS-HIGH-ENTRY-ABI-OK \
	    --require-marker PARUS-RIBON-DIRECT-HIGH-OK \
	    --require-marker XIBALBA-OK \
	    --require-marker PARUS-KERNEL-IMAGE-LAYOUT-OK \
	    --require-marker PARUS-HIGHER-HALF-CONTRACT-OK \
	    --require-marker PARUS-HIGH-VMA-LINK-OK \
	    --require-marker PARUS-AARCH64-VBAR-OK \
	    --require-marker PARUS-AARCH64-HIGHER-HALF-OK \
	    --require-marker PARUS-AARCH64-MMU-OK \
	    --require-marker EARLY_BOOT-OK \
	    --require-marker PARUS-STORAGE-EXECUTOR-OK \
	    --require-marker PARUS-STORAGE-REAL-MEDIA-RESOURCE-OK \
	    --require-marker PARUS-STORAGE-REAL-MEDIA-READONLY-SECTOR-OK \
	    --require-marker PARUS-AARCH64-BRINGUP-OK \
	    --fail-marker PARUS-LOW-TRAMPOLINE-FALLBACK-OK

qemu-uefi-parus-aarch64-runtime-smoke: require-uefi-arch $(UEFI_PARUS_DIRECT_ESP_DIR)/EFI/BOOT/$(UEFI_BOOT_FILE) $(UEFI_PARUS_DIRECT_ESP_DIR)/kernel/kernel.elf $(UEFI_PARUS_DIRECT_ESP_DIR)/ribon-direct-high $(UEFI_QEMU_AARCH64_VARS)
	@if [ "$(RIBON_ARCH)" != "aarch64" ]; then \
		echo "AArch64 Parus runtime UEFI smoke requires RIBON_ARCH=aarch64" >&2; \
		exit 2; \
	fi
	$(PYTHON) tools/qemu_uefi_smoke.py \
	    --arch aarch64 \
	    --qemu $(QEMU_AARCH64) \
	    --firmware "$(QEMU_EDK2_AARCH64_CODE)" \
	    --vars "$(UEFI_QEMU_AARCH64_VARS)" \
	    --esp $(UEFI_PARUS_DIRECT_ESP_DIR) \
	    --log $(UEFI_QEMU_PARUS_AARCH64_RUNTIME_LOG) \
	    --machine virt,gic-version=2 \
	    --cpu cortex-a72 \
	    --memory 1024M \
	    --storage-media null-co \
	    --timeout 75 \
	    --marker PARUS-KCONSOLE-OK \
	    --require-marker RIBON-UEFI-KERNEL-LOAD-START \
	    --require-marker RIBON-UEFI-KERNEL-SEGMENT-RUNTIME= \
	    --require-marker RIBON-UEFI-KERNEL-RUNTIME-ENTRY= \
	    --require-marker RIBON-UEFI-KERNEL-LOAD-OK \
	    --require-marker RIBON-UEFI-DIRECT-HIGH-REQUESTED \
	    --require-marker RIBON-UEFI-DIRECT-HIGH-PREPARED \
	    --require-marker RIBON-UEFI-FINAL-MEMORY-MAP \
	    --require-marker RIBON-UEFI-FINAL-MEMORY-MAP-DESCRIPTORS= \
	    --require-marker RIBON-UEFI-RPH1-SIZE= \
	    --require-marker RIBON-UEFI-RPH1-SECTIONS= \
	    --require-marker RIBON-UEFI-RPH1-REBUILD-ATTEMPT= \
	    --require-marker RIBON-UEFI-JUMP-ENTRY= \
	    --require-marker RIBON-UEFI-JUMP-FLAGS=0x000000000000000d \
	    --require-marker RIBON-UEFI-HANDOFF= \
	    --require-marker RIBON-UEFI-DIAG-STAGE=exit-boot-services \
	    --require-marker RIBON-UEFI-EXIT-BOOT-SERVICES-START \
	    --require-marker PARUS-HIGH-ENTRY-OK \
	    --require-marker PARUS-HIGH-ENTRY-ABI-OK \
	    --require-marker PARUS-RIBON-DIRECT-HIGH-OK \
	    --require-marker XIBALBA-OK \
	    --require-marker PARUS-KERNEL-IMAGE-LAYOUT-OK \
	    --require-marker PARUS-HIGHER-HALF-CONTRACT-OK \
	    --require-marker PARUS-HIGH-VMA-LINK-OK \
	    --require-marker PARUS-AARCH64-VBAR-OK \
	    --require-marker PARUS-AARCH64-HIGHER-HALF-OK \
	    --require-marker PARUS-AARCH64-MMU-OK \
	    --require-marker EARLY_BOOT-OK \
	    --require-marker PARUS-POSTBOOT-LOG-OK \
	    --require-marker PARUS-RUNTIME-IDLE \
	    --require-marker PARUS-KCONSOLE-OK \
	    --fail-marker PARUS-LOW-TRAMPOLINE-FALLBACK-OK

$(RPI_PACKAGE_DIR)/kernel8.img: $(RPI_IMAGE)
	@mkdir -p $(@D)
	cp $(RPI_IMAGE) $@

$(RPI_PACKAGE_DIR)/ribon-rpi5.img: $(RPI_IMAGE)
	@mkdir -p $(@D)
	cp $(RPI_IMAGE) $@

$(RPI_PACKAGE_DIR)/config.txt: $(RPI_CONFIG_TXT)
	@mkdir -p $(@D)
	cp $< $@

$(RPI_PACKAGE_DIR)/cmdline.txt: $(RPI_CMDLINE_TXT)
	@mkdir -p $(@D)
	cp $< $@

$(RPI_PACKAGE_DIR)/kernel/kernel.elf: $(RPI_PARUS_ELF)
	@mkdir -p $(@D)
	cp $< $@

rpi-package-one: $(RPI_PACKAGE_DIR)/kernel8.img $(RPI_PACKAGE_DIR)/ribon-rpi5.img $(RPI_PACKAGE_DIR)/config.txt $(RPI_PACKAGE_DIR)/cmdline.txt $(RPI_PACKAGE_DIR)/kernel/kernel.elf
	@echo "RIBON-RPI-PACKAGE=$(RPI_PACKAGE_DIR)"

rpi-package-check: rpi-package-one
	$(PYTHON) tools/check_rpi_package.py --package $(RPI_PACKAGE_DIR)

qemu-rpi-smoke:
	$(MAKE) --no-print-directory RPI_PROFILE=qemu-virt RPI_UART_BASE=$(RPI_QEMU_UART_BASE) RPI_BOARD_KIND=RIBON_RPI_BOARD_QEMU_VIRT RPI_LOAD_ADDR=0x40080000 RPI_RAM_BASE=0x40000000 RPI_KERNEL_LOAD_BASE=0x40200000 rpi-image
	$(PYTHON) tools/qemu_rpi_payload_smoke.py --qemu $(QEMU_AARCH64) --image $(BUILD_ROOT)/rpi-qemu-virt/kernel8.img --log $(BUILD_ROOT)/rpi-qemu-virt/qemu-rpi-smoke.log

metadata:
	@echo "name=Ribon Core"
	@echo "language=C"
	@echo "arch=$(RIBON_ARCH)"
	@echo "arches=$(RIBON_ARCHES)"
	@echo "build=Makefile"
	@echo "library=$(LIB_NAME)"
	@echo "bootloader=ribon-boot"
	@echo "uefi-app=ribon-uefi.efi"
	@echo "rpi-image=kernel8.img"

clean:
	rm -rf $(BUILD_ROOT)
