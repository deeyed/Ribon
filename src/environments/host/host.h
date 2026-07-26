#ifndef RIBON_ENVIRONMENTS_HOST_HOST_H
#define RIBON_ENVIRONMENTS_HOST_HOST_H

#include <Ribon/firmware/environment.h>
#include <Ribon/firmware/services.h>

/** @brief Host reference product의 immutable service table을 반환한다. */
const struct RibonServiceTable *ribon_host_services(void);

/** @brief Host reference product의 deterministic environment fixture를 수집한다. */
int ribon_host_environment_collect(
    enum RibonArchitectureId architecture,
    struct RibonBootEnvironment *out);

#endif
