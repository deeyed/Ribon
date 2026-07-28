#ifndef RIBON_PROTOCOLS_OS_ZIRCON_ZBI_H
#define RIBON_PROTOCOLS_OS_ZIRCON_ZBI_H

#include <stdint.h>

#include <Ribon/plugin/descriptor.h>

/** @brief Bounded ZBI container header를 검사한다. */
int ribon_zircon_zbi_is_valid(const void *data, uint64_t size);

/** @brief Zircon ZBI Boot Protocol plugin descriptor다. */
extern const struct RibonPluginDescriptor ribon_zircon_protocol_plugin_descriptor;

#endif
