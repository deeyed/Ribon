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

/** @brief Host in-memory source를 다음 bounded boot transaction에 결합한다. */
int ribon_host_boot_source_bind(const void *data, uint64_t size);

/** @brief Host durable journal fixture의 write/flush/quiesce observation을 초기화한다. */
void ribon_host_lifecycle_fixture_reset(void);

/** @brief Host durable journal fixture가 기록한 attempt write 수를 반환한다. */
uint32_t ribon_host_lifecycle_fixture_write_count(void);

/** @brief Host durable journal fixture가 수행한 flush 수를 반환한다. */
uint32_t ribon_host_lifecycle_fixture_flush_count(void);

/** @brief Host durable journal fixture가 수행한 quiesce 수를 반환한다. */
uint32_t ribon_host_lifecycle_fixture_quiesce_count(void);
uint32_t ribon_host_lifecycle_fixture_watchdog_arm_count(void);
uint64_t ribon_host_lifecycle_fixture_watchdog_timeout_ms(void);

/** @brief Host fixture에 남은 durable metadata byte 수를 반환한다. */
uint64_t ribon_host_lifecycle_fixture_metadata_size(void);

/** @brief 다음 source read/metadata write/flush/quiesce에 주입할 failure 횟수를 설정한다. */
void ribon_host_lifecycle_fixture_set_failures(
    uint32_t source_reads,
    uint32_t metadata_writes,
    uint32_t flushes,
    uint32_t quiesces);

/** @brief Monotonic fixture가 각 read 뒤 증가할 tick step을 설정한다. */
void ribon_host_lifecycle_fixture_set_timer_step(uint64_t step);

#endif
