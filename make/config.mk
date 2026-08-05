RIBON_ARCH ?= x86_64
RIBON_ARCHES := x86_64 aarch64 riscv64
BUILD_ROOT ?= $(ROOT)/build
BUILD_DIR ?= $(BUILD_ROOT)/$(RIBON_ARCH)

CC ?= cc
PYTHON ?= python3
QSTAR ?= qstar
OPENSSL ?= openssl
DOXYGEN ?= doxygen
SPHINX_BUILD ?= $(firstword $(wildcard $(BUILD_ROOT)/docs/venv/bin/sphinx-build) sphinx-build)

LLVM_TOOL_RESOLVER := $(ROOT)/tools/make/find_llvm_tool.py
FIRMWARE_RESOLVER := $(ROOT)/tools/make/find_firmware.py

CROSS_CC ?= $(shell $(PYTHON) $(LLVM_TOOL_RESOLVER) \
	--tool clang --require-target riscv64 2>/dev/null)
X86_64_CC ?= $(CROSS_CC)
AARCH64_CC ?= $(CROSS_CC)
RISCV64_CC ?= $(CROSS_CC)
LD_LLD ?= $(shell $(PYTHON) $(LLVM_TOOL_RESOLVER) --tool ld.lld 2>/dev/null)
LLD_LINK ?= $(shell $(PYTHON) $(LLVM_TOOL_RESOLVER) --tool lld-link 2>/dev/null)
OBJCOPY ?= $(shell $(PYTHON) $(LLVM_TOOL_RESOLVER) --tool llvm-objcopy 2>/dev/null)
LLVM_AR ?= $(shell $(PYTHON) $(LLVM_TOOL_RESOLVER) --tool llvm-ar 2>/dev/null)
QEMU_AARCH64 ?= qemu-system-aarch64
QEMU_X86_64 ?= qemu-system-x86_64
QEMU_RISCV64 ?= qemu-system-riscv64

ifeq ($(origin AR),default)
AR := $(LLVM_AR)
endif

RISCV64_OPENSBI_FIRMWARE ?= $(shell $(PYTHON) $(FIRMWARE_RESOLVER) \
	--kind opensbi-riscv64 --qemu $(QEMU_RISCV64) 2>/dev/null)
X86_64_UEFI_FIRMWARE ?= $(shell $(PYTHON) $(FIRMWARE_RESOLVER) \
	--kind uefi-x86_64 --qemu $(QEMU_X86_64) 2>/dev/null)

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
