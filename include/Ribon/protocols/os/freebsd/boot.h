#ifndef RIBON_PROTOCOLS_OS_FREEBSD_BOOT_H
#define RIBON_PROTOCOLS_OS_FREEBSD_BOOT_H

#include <Ribon/plugin/descriptor.h>

/**
 * @brief FreeBSD loader EFI chainload package descriptor다.
 *
 * Generic terminal-image launcher가 검증된 loader.efi를 firmware-managed image로 실행하며,
 * FreeBSD kernel/module staging과 ExitBootServices 권한은 loader에 남긴다.
 */
extern const struct RibonPluginDescriptor ribon_freebsd_protocol_plugin_descriptor;

#endif
