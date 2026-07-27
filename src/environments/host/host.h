#ifndef RIBON_ENVIRONMENTS_HOST_HOST_H
#define RIBON_ENVIRONMENTS_HOST_HOST_H

#include <Ribon/firmware/environment.h>
#include <Ribon/service/directory.h>

/** @brief Host reference product의 immutable typed service directory를 반환한다. */
const struct RibonServiceDirectory *ribon_host_service_directory(void);

/** @brief Host reference product의 deterministic environment fixture를 수집한다. */
int ribon_host_environment_collect(
    enum RibonArchitectureId architecture,
    struct RibonBootEnvironment *out);

#endif
