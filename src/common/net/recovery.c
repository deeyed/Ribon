#include <Ribon/network/recovery.h>

#include <string.h>

/** @brief Stable ID와 path byte sequence가 같은지 검사한다. */
static int network_streq(const char *left, const char *right)
{
    if (left == NULL || right == NULL) {
        return 0;
    }
    while (*left != '\0' && *right != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

/** @brief TFTP path가 relative bounded product constant인지 검사한다. */
static int path_is_canonical(const char *path)
{
    size_t length = 0u;
    int previous_dot = 0;
    if (path == NULL || path[0] == '\0' || path[0] == '/') {
        return 0;
    }
    while (path[length] != '\0') {
        const unsigned char character = (unsigned char)path[length];
        const int accepted =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '.' || character == '_' || character == '-' ||
            character == '/';
        if (!accepted || length >= RIBON_RECOVERY_NETWORK_PATH_CAPACITY ||
            (character == '.' && previous_dot)) {
            return 0;
        }
        previous_dot = character == '.';
        ++length;
    }
    return length != 0u && path[length - 1u] != '/';
}

/** @brief Fixed-size byte region이 모두 0인지 검사한다. */
static int bytes_are_zero(const void *bytes, size_t size)
{
    const uint8_t *cursor = bytes;
    size_t index;
    for (index = 0u; index < size; ++index) {
        if (cursor[index] != 0u) {
            return 0;
        }
    }
    return 1;
}

/** @brief Network transport service operation table의 exact typed ABI를 검사한다. */
int ribon_network_transport_service_operations_are_valid(
    const struct RibonServiceDescriptor *descriptor)
{
    const struct RibonNetworkTransportServiceOperations *operations;
    if (descriptor == NULL ||
        descriptor->kind != RIBON_SERVICE_KIND_NETWORK_TRANSPORT ||
        descriptor->provides != RIBON_CAP_NETWORK_TRANSPORT ||
        descriptor->operations_size != sizeof(*operations) ||
        descriptor->operations_abi != RIBON_SERVICE_ABI_VERSION) {
        return 0;
    }
    operations = descriptor->operations;
    return operations != NULL && operations->size == sizeof(*operations) &&
        operations->abi_version == RIBON_SERVICE_ABI_VERSION &&
        operations->fetch != NULL;
}

/** @brief Product network binding의 path, peer, budget와 object closure를 검사한다. */
int ribon_recovery_network_binding_validate(
    const struct RibonRecoveryNetworkProductBinding *binding)
{
    uint32_t index;
    if (binding == NULL || binding->size != sizeof(*binding) ||
        binding->abi_version != RIBON_RECOVERY_NETWORK_ABI_VERSION ||
        binding->transport_class < RIBON_RECOVERY_NETWORK_TRANSPORT_UEFI_BOUNDED_TFTP ||
        binding->transport_class > RIBON_RECOVERY_NETWORK_TRANSPORT_REFERENCE ||
        binding->flags != 0u || binding->service_id == NULL ||
        binding->service_id[0] == '\0' || binding->server_ipv4[0] == 0u ||
        binding->server_ipv4[0] >= 224u || binding->station_ipv4[0] == 0u ||
        binding->station_ipv4[0] >= 224u || binding->subnet_mask_ipv4[0] == 0u ||
        binding->block_size < 512u ||
        binding->block_size > 1468u || binding->retry_count > 3u ||
        binding->absolute_deadline_ms <= binding->retry_count ||
        binding->absolute_deadline_ms > 30000u ||
        !bytes_are_zero(binding->reserved, sizeof(binding->reserved))) {
        return RIBON_RECOVERY_NETWORK_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0u; index < RIBON_RECOVERY_NETWORK_OBJECT_COUNT; ++index) {
        const struct RibonRecoveryNetworkObjectBinding *object =
            &binding->objects[index];
        if (object->kind != (enum RibonRecoveryNetworkObjectKind)(index + 1u) ||
            object->flags != 0u || !path_is_canonical(object->path) ||
            object->maximum_bytes == 0u ||
            object->maximum_bytes > RIBON_RECOVERY_NETWORK_MAX_OBJECT_BYTES ||
            object->maximum_bytes >=
                (uint64_t)(UINT16_MAX - 1u) * binding->block_size) {
            return RIBON_RECOVERY_NETWORK_STATUS_INVALID_ARGUMENT;
        }
    }
    return RIBON_RECOVERY_NETWORK_STATUS_OK;
}

/** @brief Stable object role의 immutable product row를 반환한다. */
static const struct RibonRecoveryNetworkObjectBinding *find_object(
    const struct RibonRecoveryNetworkProductBinding *binding,
    enum RibonRecoveryNetworkObjectKind kind)
{
    uint32_t index;
    for (index = 0u; index < RIBON_RECOVERY_NETWORK_OBJECT_COUNT; ++index) {
        if (binding->objects[index].kind == kind) {
            return &binding->objects[index];
        }
    }
    return NULL;
}

/** @brief Product-selected provider에서 object 하나를 bounded retry로 가져온다. */
int ribon_recovery_network_fetch(
    const struct RibonRecoveryNetworkProductBinding *binding,
    const struct RibonServiceDescriptor *service,
    enum RibonRecoveryNetworkObjectKind kind,
    void *buffer,
    uint64_t buffer_capacity,
    struct RibonRecoveryNetworkResult *result)
{
    const struct RibonRecoveryNetworkObjectBinding *object;
    const struct RibonNetworkTransportServiceOperations *operations;
    uint32_t attempt;
    int status;
    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }
    status = ribon_recovery_network_binding_validate(binding);
    if (status != RIBON_RECOVERY_NETWORK_STATUS_OK || service == NULL ||
        buffer == NULL || result == NULL || buffer_capacity == 0u ||
        service->kind != RIBON_SERVICE_KIND_NETWORK_TRANSPORT ||
        !network_streq(service->id, binding->service_id) ||
        !ribon_network_transport_service_operations_are_valid(service)) {
        return RIBON_RECOVERY_NETWORK_STATUS_INVALID_ARGUMENT;
    }
    object = find_object(binding, kind);
    if (object == NULL || object->maximum_bytes > buffer_capacity ||
        object->maximum_bytes > service->output_budget) {
        return RIBON_RECOVERY_NETWORK_STATUS_CAPACITY;
    }
    operations = service->operations;
    for (attempt = 0u; attempt <= binding->retry_count; ++attempt) {
        struct RibonRecoveryNetworkRequest request = {
            .size = sizeof(request),
            .abi_version = RIBON_RECOVERY_NETWORK_ABI_VERSION,
            .object_kind = kind,
            .attempt_index = attempt,
            .path = object->path,
            .maximum_bytes = object->maximum_bytes,
            .buffer = buffer,
            .buffer_capacity = buffer_capacity,
            .server_ipv4 = {
                binding->server_ipv4[0], binding->server_ipv4[1],
                binding->server_ipv4[2], binding->server_ipv4[3],
            },
            .block_size = binding->block_size,
            .deadline_ms = binding->absolute_deadline_ms /
                ((uint32_t)binding->retry_count + 1u),
        };
        struct RibonRecoveryNetworkResult attempt_result = {
            .size = sizeof(attempt_result),
            .abi_version = RIBON_RECOVERY_NETWORK_ABI_VERSION,
            .object_kind = kind,
        };
        memset(buffer, 0, (size_t)object->maximum_bytes);
        status = operations->fetch(
            operations->context, &request, &attempt_result);
        if (status == RIBON_RECOVERY_NETWORK_STATUS_OK) {
            if (attempt_result.size != sizeof(attempt_result) ||
                attempt_result.abi_version != RIBON_RECOVERY_NETWORK_ABI_VERSION ||
                attempt_result.object_kind != kind ||
                attempt_result.bytes_received == 0u ||
                attempt_result.bytes_received > object->maximum_bytes ||
                attempt_result.bytes_received > buffer_capacity) {
                memset(buffer, 0, (size_t)object->maximum_bytes);
                return RIBON_RECOVERY_NETWORK_STATUS_MALFORMED;
            }
            *result = attempt_result;
            result->attempts_used = attempt + 1u;
            return RIBON_RECOVERY_NETWORK_STATUS_OK;
        }
        if (status != RIBON_RECOVERY_NETWORK_STATUS_TIMEOUT &&
            status != RIBON_RECOVERY_NETWORK_STATUS_IO) {
            break;
        }
    }
    memset(buffer, 0, (size_t)object->maximum_bytes);
    return status;
}

/** @brief Recovery network status의 안정적인 diagnostic 이름을 반환한다. */
const char *ribon_recovery_network_status_name(
    enum RibonRecoveryNetworkStatus status)
{
    switch (status) {
    case RIBON_RECOVERY_NETWORK_STATUS_OK: return "ok";
    case RIBON_RECOVERY_NETWORK_STATUS_INVALID_ARGUMENT: return "invalid-argument";
    case RIBON_RECOVERY_NETWORK_STATUS_UNSUPPORTED: return "unsupported";
    case RIBON_RECOVERY_NETWORK_STATUS_CAPACITY: return "capacity";
    case RIBON_RECOVERY_NETWORK_STATUS_TIMEOUT: return "timeout";
    case RIBON_RECOVERY_NETWORK_STATUS_IO: return "io";
    case RIBON_RECOVERY_NETWORK_STATUS_MALFORMED: return "malformed";
    case RIBON_RECOVERY_NETWORK_STATUS_AMBIGUOUS: return "ambiguous";
    case RIBON_RECOVERY_NETWORK_STATUS_TRUNCATED: return "truncated";
    default: return "unknown";
    }
}
