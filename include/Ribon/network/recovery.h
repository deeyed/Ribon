#ifndef RIBON_NETWORK_RECOVERY_H
#define RIBON_NETWORK_RECOVERY_H

#include <stddef.h>
#include <stdint.h>

#include <Ribon/service/directory.h>

/** @brief Recovery-only bounded fetch ABI version이다. */
#define RIBON_RECOVERY_NETWORK_ABI_VERSION 1u

/** @brief Product가 고정할 수 있는 object 수다. */
#define RIBON_RECOVERY_NETWORK_OBJECT_COUNT 3u

/** @brief 한 product-selected TFTP path의 NUL 제외 byte 상한이다. */
#define RIBON_RECOVERY_NETWORK_PATH_CAPACITY 96u

/** @brief Recovery transport가 반환할 수 있는 단일 object의 절대 상한이다. */
#define RIBON_RECOVERY_NETWORK_MAX_OBJECT_BYTES (64ull * 1024ull * 1024ull)

/** @brief Product graph가 선택하는 bounded transport class다. */
enum RibonRecoveryNetworkTransportClass {
    RIBON_RECOVERY_NETWORK_TRANSPORT_INVALID = 0,
    RIBON_RECOVERY_NETWORK_TRANSPORT_UEFI_BOUNDED_TFTP = 1,
    RIBON_RECOVERY_NETWORK_TRANSPORT_NATIVE_TFTP = 2,
    RIBON_RECOVERY_NETWORK_TRANSPORT_REFERENCE = 3,
};

/** @brief Signed update transaction이 가져오는 stable object role이다. */
enum RibonRecoveryNetworkObjectKind {
    RIBON_RECOVERY_NETWORK_OBJECT_INVALID = 0,
    RIBON_RECOVERY_NETWORK_OBJECT_MANIFEST = 1,
    RIBON_RECOVERY_NETWORK_OBJECT_SIGNATURE_ENVELOPE = 2,
    RIBON_RECOVERY_NETWORK_OBJECT_BUNDLE = 3,
};

/** @brief Product-selected object identity와 output 상한이다. */
struct RibonRecoveryNetworkObjectBinding {
    enum RibonRecoveryNetworkObjectKind kind; /**< Stable object role이다. */
    uint32_t flags; /**< v1에서는 0이다. */
    const char *path; /**< Product가 고정한 canonical relative TFTP path다. */
    uint64_t maximum_bytes; /**< Exact fetch 결과가 넘을 수 없는 byte 수다. */
};

/** @brief Generated product graph가 transport adapter와 공유하는 immutable binding이다. */
struct RibonRecoveryNetworkProductBinding {
    uint32_t size; /**< `sizeof(struct RibonRecoveryNetworkProductBinding)`이다. */
    uint32_t abi_version; /**< `RIBON_RECOVERY_NETWORK_ABI_VERSION`이다. */
    enum RibonRecoveryNetworkTransportClass transport_class; /**< Target adapter class다. */
    uint32_t flags; /**< v1에서는 0이다. */
    const char *service_id; /**< Exact network-transport service ID다. */
    uint8_t server_ipv4[4]; /**< Product-provisioned IPv4 TFTP peer다. */
    uint8_t station_ipv4[4]; /**< Product-provisioned 단일 client IPv4 주소다. */
    uint8_t subnet_mask_ipv4[4]; /**< Product-provisioned client subnet mask다. */
    uint16_t block_size; /**< 요청할 bounded TFTP block byte 수다. */
    uint16_t retry_count; /**< 첫 시도 뒤 허용하는 추가 시도 수다. */
    uint32_t absolute_deadline_ms; /**< 모든 retry를 합한 단일 object deadline이다. */
    struct RibonRecoveryNetworkObjectBinding
        objects[RIBON_RECOVERY_NETWORK_OBJECT_COUNT]; /**< Stable role 순 object table이다. */
    uint64_t reserved[4]; /**< v1에서는 모두 0이다. */
};

/** @brief Generic retry gate가 target adapter에 전달하는 단일 시도 요청이다. */
struct RibonRecoveryNetworkRequest {
    uint32_t size; /**< `sizeof(struct RibonRecoveryNetworkRequest)`이다. */
    uint32_t abi_version; /**< `RIBON_RECOVERY_NETWORK_ABI_VERSION`이다. */
    enum RibonRecoveryNetworkObjectKind object_kind; /**< 요청 object role이다. */
    uint32_t attempt_index; /**< 0부터 시작하는 bounded attempt index다. */
    const char *path; /**< Product binding에서 빌린 canonical relative path다. */
    uint64_t maximum_bytes; /**< Product가 허용한 exact output 상한이다. */
    void *buffer; /**< Caller-owned output buffer다. */
    uint64_t buffer_capacity; /**< Caller-owned buffer byte 수다. */
    uint8_t server_ipv4[4]; /**< Product-provisioned exact peer다. */
    uint16_t block_size; /**< 요청할 TFTP block byte 수다. */
    uint16_t reserved0; /**< 0이어야 한다. */
    uint32_t deadline_ms; /**< 이 시도가 넘을 수 없는 deadline이다. */
};

/** @brief Provider와 generic retry gate가 공유하는 fetch receipt다. */
struct RibonRecoveryNetworkResult {
    uint32_t size; /**< `sizeof(struct RibonRecoveryNetworkResult)`이다. */
    uint32_t abi_version; /**< `RIBON_RECOVERY_NETWORK_ABI_VERSION`이다. */
    enum RibonRecoveryNetworkObjectKind object_kind; /**< 완료된 object role이다. */
    uint32_t attempts_used; /**< Generic gate가 소비한 총 시도 수다. */
    uint64_t bytes_received; /**< Caller buffer에 확정된 exact byte 수다. */
};

/** @brief Bounded recovery fetch의 stable fail-closed 결과다. */
enum RibonRecoveryNetworkStatus {
    RIBON_RECOVERY_NETWORK_STATUS_OK = 0,
    RIBON_RECOVERY_NETWORK_STATUS_INVALID_ARGUMENT = -1,
    RIBON_RECOVERY_NETWORK_STATUS_UNSUPPORTED = -2,
    RIBON_RECOVERY_NETWORK_STATUS_CAPACITY = -3,
    RIBON_RECOVERY_NETWORK_STATUS_TIMEOUT = -4,
    RIBON_RECOVERY_NETWORK_STATUS_IO = -5,
    RIBON_RECOVERY_NETWORK_STATUS_MALFORMED = -6,
    RIBON_RECOVERY_NETWORK_STATUS_AMBIGUOUS = -7,
    RIBON_RECOVERY_NETWORK_STATUS_TRUNCATED = -8,
};

/** @brief Product network binding의 path, peer, budget와 object closure를 검사한다. */
int ribon_recovery_network_binding_validate(
    const struct RibonRecoveryNetworkProductBinding *binding);

/**
 * @brief Product-selected provider에서 object 하나를 bounded retry로 가져온다.
 *
 * @param binding Generated immutable product binding이다.
 * @param service Recovery/provisioning graph가 선택한 exact transport service다.
 * @param kind Manifest, signature envelope 또는 bundle role이다.
 * @param buffer 호출자가 소유하는 고정 용량 staging buffer다.
 * @param buffer_capacity `buffer`의 byte 수다.
 * @param result 성공한 exact byte 수와 attempt 수를 받는 caller-owned receipt다.
 * @return 서명이나 digest 신뢰를 부여하지 않고 transport 결과만 반환한다.
 */
int ribon_recovery_network_fetch(
    const struct RibonRecoveryNetworkProductBinding *binding,
    const struct RibonServiceDescriptor *service,
    enum RibonRecoveryNetworkObjectKind kind,
    void *buffer,
    uint64_t buffer_capacity,
    struct RibonRecoveryNetworkResult *result);

/** @brief Recovery network status의 안정적인 diagnostic 이름을 반환한다. */
const char *ribon_recovery_network_status_name(
    enum RibonRecoveryNetworkStatus status);

#endif
