#ifndef RIBON_PROTOCOLS_OS_LINUX_BOOT_H
#define RIBON_PROTOCOLS_OS_LINUX_BOOT_H

#include <Ribon/plugin/descriptor.h>

/** @brief Linux raw-FDT entry Boot Protocol plugin descriptor다. */
extern const struct RibonPluginDescriptor ribon_linux_protocol_plugin_descriptor;

/** @brief Linux x86_64 EFI-stub firmware-managed Boot Protocol descriptor다. */
extern const struct RibonPluginDescriptor ribon_linux_efi_protocol_plugin_descriptor;

#endif
