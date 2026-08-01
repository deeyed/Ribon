#include <Ribon/network/recovery.h>
#include <Ribon/network/tftp.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct FetchFixture {
    uint32_t calls;
    uint32_t timeout_calls;
    uint64_t returned_bytes;
    int final_status;
};

static int failures;

/** @brief 조건 실패를 process exit status에 누적한다. */
static void expect(int condition, const char *name)
{
    if (!condition) {
        (void)fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

/** @brief Generic retry gate에 timeout 뒤 deterministic bytes를 제공한다. */
static int fixture_fetch(
    void *opaque,
    const struct RibonRecoveryNetworkRequest *request,
    struct RibonRecoveryNetworkResult *result)
{
    struct FetchFixture *fixture = opaque;
    ++fixture->calls;
    if (fixture->calls <= fixture->timeout_calls) {
        return RIBON_RECOVERY_NETWORK_STATUS_TIMEOUT;
    }
    if (fixture->final_status != RIBON_RECOVERY_NETWORK_STATUS_OK) {
        return fixture->final_status;
    }
    if (fixture->returned_bytes <= request->buffer_capacity) {
        memset(request->buffer, (int)request->object_kind,
            (size_t)fixture->returned_bytes);
    }
    result->bytes_received = fixture->returned_bytes;
    return RIBON_RECOVERY_NETWORK_STATUS_OK;
}

/** @brief Canonical reference binding을 만든다. */
static struct RibonRecoveryNetworkProductBinding binding(void)
{
    return (struct RibonRecoveryNetworkProductBinding){
        .size = sizeof(struct RibonRecoveryNetworkProductBinding),
        .abi_version = RIBON_RECOVERY_NETWORK_ABI_VERSION,
        .transport_class = RIBON_RECOVERY_NETWORK_TRANSPORT_REFERENCE,
        .service_id = "service.test.network",
        .server_ipv4 = {10u, 0u, 2u, 2u},
        .station_ipv4 = {10u, 0u, 2u, 15u},
        .subnet_mask_ipv4 = {255u, 255u, 255u, 0u},
        .block_size = 512u,
        .retry_count = 1u,
        .absolute_deadline_ms = 30000u,
        .objects = {
            {
                .kind = RIBON_RECOVERY_NETWORK_OBJECT_MANIFEST,
                .path = "update.man",
                .maximum_bytes = 64u,
            },
            {
                .kind = RIBON_RECOVERY_NETWORK_OBJECT_SIGNATURE_ENVELOPE,
                .path = "update.sig",
                .maximum_bytes = 64u,
            },
            {
                .kind = RIBON_RECOVERY_NETWORK_OBJECT_BUNDLE,
                .path = "update.bin",
                .maximum_bytes = 64u,
            },
        },
    };
}

/** @brief Generic binding, service routing, retry와 fail-closed output wipe를 검사한다. */
static void test_fetch_gate(void)
{
    struct FetchFixture fixture = {
        .timeout_calls = 1u,
        .returned_bytes = 8u,
        .final_status = RIBON_RECOVERY_NETWORK_STATUS_OK,
    };
    const struct RibonNetworkTransportServiceOperations operations = {
        .size = sizeof(operations),
        .abi_version = RIBON_SERVICE_ABI_VERSION,
        .context = &fixture,
        .fetch = fixture_fetch,
    };
    const struct RibonServiceDescriptor service = {
        .magic = RIBON_SERVICE_DESCRIPTOR_MAGIC,
        .size = sizeof(service),
        .abi_version = RIBON_SERVICE_ABI_VERSION,
        .kind = RIBON_SERVICE_KIND_NETWORK_TRANSPORT,
        .cardinality = RIBON_SERVICE_CARDINALITY_AUTHORITY,
        .lifetime = RIBON_SERVICE_LIFETIME_QUIESCE,
        .phase = RIBON_PLUGIN_PHASE_BOOT,
        .id = "service.test.network",
        .provides = RIBON_CAP_NETWORK_TRANSPORT,
        .architecture_mask = RIBON_ARCH_MASK_ALL,
        .environment_mask = RIBON_ENV_MASK_HOST,
        .mode_mask = RIBON_MODE_MASK(RIBON_MODE_RECOVERY),
        .arena_budget = 1u,
        .input_budget = 96u,
        .output_budget = 64u,
        .deadline_ms = 30000u,
        .operations = &operations,
        .operations_size = sizeof(operations),
        .operations_abi = RIBON_SERVICE_ABI_VERSION,
        .validate_operations =
            ribon_network_transport_service_operations_are_valid,
    };
    struct RibonRecoveryNetworkProductBinding product = binding();
    struct RibonRecoveryNetworkResult result;
    uint8_t output[64];
    int status;

    expect(ribon_recovery_network_binding_validate(&product) ==
        RIBON_RECOVERY_NETWORK_STATUS_OK, "binding-positive");
    status = ribon_recovery_network_fetch(
        &product, &service, RIBON_RECOVERY_NETWORK_OBJECT_BUNDLE,
        output, sizeof(output), &result);
    expect(status == RIBON_RECOVERY_NETWORK_STATUS_OK, "fetch-positive");
    expect(result.bytes_received == 8u && result.attempts_used == 2u,
        "fetch-retry-receipt");
    expect(output[0] == RIBON_RECOVERY_NETWORK_OBJECT_BUNDLE,
        "fetch-output");

    fixture.calls = 0u;
    fixture.timeout_calls = 0u;
    fixture.returned_bytes = 65u;
    memset(output, 0xa5, sizeof(output));
    status = ribon_recovery_network_fetch(
        &product, &service, RIBON_RECOVERY_NETWORK_OBJECT_BUNDLE,
        output, sizeof(output), &result);
    expect(status == RIBON_RECOVERY_NETWORK_STATUS_MALFORMED,
        "provider-oversize");
    expect(output[0] == 0u && output[63] == 0u, "provider-oversize-wipe");

    expect(ribon_recovery_network_fetch(
        &product, &service, RIBON_RECOVERY_NETWORK_OBJECT_BUNDLE,
        output, 32u, &result) == RIBON_RECOVERY_NETWORK_STATUS_CAPACITY,
        "caller-capacity");
    product.objects[0].path = "../update.man";
    expect(ribon_recovery_network_binding_validate(&product) ==
        RIBON_RECOVERY_NETWORK_STATUS_INVALID_ARGUMENT, "path-traversal");
    product = binding();
    product.retry_count = 4u;
    expect(ribon_recovery_network_binding_validate(&product) ==
        RIBON_RECOVERY_NETWORK_STATUS_INVALID_ARGUMENT, "retry-bound");
    product = binding();
    product.objects[1].kind = RIBON_RECOVERY_NETWORK_OBJECT_BUNDLE;
    expect(ribon_recovery_network_binding_validate(&product) ==
        RIBON_RECOVERY_NETWORK_STATUS_INVALID_ARGUMENT, "role-order");
}

/** @brief TFTP DATA packet을 caller buffer에 조립한다. */
static size_t data_packet(
    uint8_t *packet,
    uint16_t block,
    size_t payload_size)
{
    packet[0] = 0u;
    packet[1] = 3u;
    packet[2] = (uint8_t)(block >> 8u);
    packet[3] = (uint8_t)block;
    memset(packet + 4u, (int)block, payload_size);
    return payload_size + 4u;
}

/** @brief OACK, duplicate, reorder, final short block와 malformed corpus를 검사한다. */
static void test_tftp_guard(void)
{
    struct RibonTftpGuard guard;
    uint8_t packet[520];
    const uint8_t *payload;
    size_t payload_size;
    static const uint8_t oack[] = {
        0u, 6u, 'b','l','k','s','i','z','e',0u,'5','1','2',0u,
    };
    static const uint8_t malformed_oack[] = {
        0u, 6u, 't','s','i','z','e',0u,'8',0u,
    };
    static const uint8_t remote_error[] = {0u, 5u, 0u, 1u, 0u};

    expect(ribon_tftp_guard_initialize(&guard, 512u, 1024u) ==
        RIBON_TFTP_GUARD_STATUS_OK, "tftp-init");
    expect(ribon_tftp_guard_accept(
        &guard, oack, sizeof(oack), &payload, &payload_size) ==
        RIBON_TFTP_GUARD_STATUS_OK, "tftp-oack");
    expect(ribon_tftp_guard_accept(
        &guard, packet, data_packet(packet, 1u, 512u),
        &payload, &payload_size) == RIBON_TFTP_GUARD_STATUS_OK &&
        payload_size == 512u, "tftp-data-1");
    expect(ribon_tftp_guard_accept(
        &guard, packet, data_packet(packet, 1u, 512u),
        &payload, &payload_size) == RIBON_TFTP_GUARD_STATUS_DUPLICATE &&
        payload_size == 0u, "tftp-duplicate");
    expect(ribon_tftp_guard_accept(
        &guard, packet, data_packet(packet, 3u, 1u),
        &payload, &payload_size) == RIBON_TFTP_GUARD_STATUS_OUT_OF_ORDER,
        "tftp-reorder");
    expect(ribon_tftp_guard_accept(
        &guard, packet, data_packet(packet, 2u, 0u),
        &payload, &payload_size) == RIBON_TFTP_GUARD_STATUS_COMPLETE &&
        guard.received_bytes == 512u, "tftp-final");

    expect(ribon_tftp_guard_initialize(&guard, 512u, 511u) ==
        RIBON_TFTP_GUARD_STATUS_OK, "tftp-small-init");
    expect(ribon_tftp_guard_accept(
        &guard, packet, data_packet(packet, 1u, 512u),
        &payload, &payload_size) == RIBON_TFTP_GUARD_STATUS_CAPACITY,
        "tftp-capacity");
    expect(ribon_tftp_guard_initialize(&guard, 512u, 512u) ==
        RIBON_TFTP_GUARD_STATUS_OK, "tftp-hostile-init");
    expect(ribon_tftp_guard_accept(
        &guard, packet, data_packet(packet, 0u, 1u),
        &payload, &payload_size) == RIBON_TFTP_GUARD_STATUS_OUT_OF_ORDER,
        "tftp-block-zero");
    expect(ribon_tftp_guard_accept(
        &guard, malformed_oack, sizeof(malformed_oack),
        &payload, &payload_size) ==
        RIBON_TFTP_GUARD_STATUS_UNSUPPORTED_OPTION, "tftp-option");
    expect(ribon_tftp_guard_accept(
        &guard, remote_error, sizeof(remote_error),
        &payload, &payload_size) == RIBON_TFTP_GUARD_STATUS_REMOTE_ERROR,
        "tftp-remote-error");
}

/** @brief Seeded hostile packet corpus를 sanitizer와 같이 돌린다. */
static void test_tftp_hostile_corpus(void)
{
    uint32_t state = UINT32_C(0x5249424f);
    uint8_t packet[520];
    uint32_t iteration;
    for (iteration = 0u; iteration < 10000u; ++iteration) {
        struct RibonTftpGuard guard;
        const uint8_t *payload;
        size_t payload_size;
        size_t packet_size;
        size_t index;
        int status;
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        packet_size = state % (sizeof(packet) + 1u);
        for (index = 0u; index < packet_size; ++index) {
            state = state * UINT32_C(1664525) + UINT32_C(1013904223);
            packet[index] = (uint8_t)(state >> 24u);
        }
        expect(ribon_tftp_guard_initialize(&guard, 512u, 4096u) ==
            RIBON_TFTP_GUARD_STATUS_OK, "tftp-corpus-init");
        status = ribon_tftp_guard_accept(
            &guard, packet, packet_size, &payload, &payload_size);
        expect(status >= RIBON_TFTP_GUARD_STATUS_REMOTE_ERROR &&
            status <= RIBON_TFTP_GUARD_STATUS_COMPLETE,
            "tftp-corpus-status-domain");
        expect(payload_size <= 512u, "tftp-corpus-payload-bound");
    }
}

int main(void)
{
    test_fetch_gate();
    test_tftp_guard();
    test_tftp_hostile_corpus();
    if (failures != 0) {
        (void)fprintf(stderr, "network tests failed: %d\n", failures);
        return 1;
    }
    (void)puts(
        "RIBON-D05-RECOVERY-NETWORK-UNIT-OK "
        "retry=bounded duplicate=accepted reorder=rejected option=bounded "
        "hostile-corpus=10000");
    return 0;
}
