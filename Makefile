.DEFAULT_GOAL := all

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
RIBON_MAKE_DIR := $(ROOT)/make
RIBON_MODEL_MAKEFILE := $(RIBON_MAKE_DIR)/model.mk
RIBON_MAKEFILES := \
	$(ROOT)/Makefile \
	$(RIBON_MAKE_DIR)/config.mk \
	$(RIBON_MODEL_MAKEFILE) \
	$(RIBON_MAKE_DIR)/rules/tooling.mk \
	$(RIBON_MAKE_DIR)/rules/core.mk \
	$(RIBON_MAKE_DIR)/rules/ribos.mk \
	$(RIBON_MAKE_DIR)/rules/security-update.mk \
	$(RIBON_MAKE_DIR)/rules/raw-fdt.mk \
	$(RIBON_MAKE_DIR)/rules/uefi-bios.mk \
	$(RIBON_MAKE_DIR)/rules/host-sdk.mk \
	$(RIBON_MAKE_DIR)/rules/aggregate.mk

include $(RIBON_MAKE_DIR)/config.mk
include $(RIBON_MODEL_MAKEFILE)
include $(RIBON_MAKE_DIR)/rules/tooling.mk
include $(RIBON_MAKE_DIR)/rules/core.mk
include $(RIBON_MAKE_DIR)/rules/ribos.mk
include $(RIBON_MAKE_DIR)/rules/security-update.mk
include $(RIBON_MAKE_DIR)/rules/raw-fdt.mk
include $(RIBON_MAKE_DIR)/rules/uefi-bios.mk
include $(RIBON_MAKE_DIR)/rules/host-sdk.mk
include $(RIBON_MAKE_DIR)/rules/aggregate.mk
