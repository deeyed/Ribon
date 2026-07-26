#ifndef RIBON_SDK_HOST_H
#define RIBON_SDK_HOST_H

#include <stdint.h>

#include <Ribon/sdk/package.h>

/**
 * @brief Host contract harness에서 package kind와 capability를 검사한다.
 *
 * Native firmware, source-private header, 전역 registry 없이 descriptor만 검증한다.
 */
int ribon_sdk_host_validate_package(
    const struct RibonSdkPluginPackage *package,
    enum RibonPluginKind expected_kind,
    uint64_t required_provides,
    uint64_t forbidden_requires);

#endif
