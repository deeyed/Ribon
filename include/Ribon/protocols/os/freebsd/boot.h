#ifndef RIBON_PROTOCOLS_OS_FREEBSD_BOOT_H
#define RIBON_PROTOCOLS_OS_FREEBSD_BOOT_H

#include <Ribon/plugin/descriptor.h>

/**
 * @brief FreeBSD loader EFI chainload package descriptor다.
 *
 * Current direct-transfer transaction cannot preserve EFI Boot Services, so the package
 * fails closed until a typed firmware StartImage transport is selected.
 */
extern const struct RibonPluginDescriptor ribon_freebsd_protocol_plugin_descriptor;

#endif
