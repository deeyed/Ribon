#ifndef RIBON_PLATFORM_DIAGNOSTIC_H
#define RIBON_PLATFORM_DIAGNOSTIC_H

#include <Ribon/platform/facts.h>

/** @brief Selected platform의 bounded polling diagnostic sink를 준비한다. */
int ribon_platform_diagnostic_initialize(
    const struct RibonPlatformFacts *facts);

/** @brief Selected platform diagnostic sink에 NUL-terminated text를 쓴다. */
int ribon_platform_diagnostic_write(const char *text);

#endif
