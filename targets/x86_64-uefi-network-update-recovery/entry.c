#include "../../src/environments/uefi-app/uefi_app.h"
#include "../../src/environments/uefi-app/update_storage.h"
#include "../../src/environments/uefi-app/recovery_network.h"

#include <Ribon/core/capability.h>
#include <Ribon/network/recovery.h>
#include <Ribon/plugin/registry.h>
#include <Ribon/port/port.h>
#include <Ribon/security/protected_state.h>
#include <Ribon/update/transaction.h>

#include <string.h>

#define NETWORK_MEMORY_MAP_CAPACITY (64u * 1024u)
#define NETWORK_REGION_CAPACITY 256u
#define NETWORK_MANIFEST_CAPACITY 4096u
#define NETWORK_ENVELOPE_CAPACITY 512u
#define NETWORK_BUNDLE_CAPACITY (64u * 1024u)
#define NETWORK_SCRATCH_CAPACITY (64u * 1024u)

static _Alignas(16) uint8_t raw_memory_map[NETWORK_MEMORY_MAP_CAPACITY];
static struct RibonMemoryRegion memory_regions[NETWORK_REGION_CAPACITY];
static _Alignas(4096) uint8_t manifest_bytes[NETWORK_MANIFEST_CAPACITY];
static _Alignas(4096) uint8_t envelope_bytes[NETWORK_ENVELOPE_CAPACITY];
static _Alignas(4096) uint8_t bundle_bytes[NETWORK_BUNDLE_CAPACITY];
static _Alignas(4096) uint8_t install_scratch[NETWORK_SCRATCH_CAPACITY];
static struct RibonUefiUpdateStorageContext update_storage;
static const struct RibonDiagnosticSinkServiceOperations *diagnostic_sink;

struct MemoryBundle {
    const uint8_t *bytes;
    uint64_t size;
};

/** @brief Boot Services lifetime의 diagnostic sink에 stable marker를 기록한다. */
static void marker(const char *text)
{
    uint64_t length = 0u;
    if (diagnostic_sink == NULL || text == NULL) {
        return;
    }
    while (text[length] != '\0') {
        ++length;
    }
    (void)diagnostic_sink->write(diagnostic_sink->context, text, length);
    (void)diagnostic_sink->write(diagnostic_sink->context, "\r\n", 2u);
}

/** @brief 실패 stage를 fail-closed terminal marker로 남긴다. */
static EFI_STATUS fail(const char *stage)
{
    marker("RIBON-D05-NETWORK-UPDATE-FAIL");
    marker(stage);
    return EFI_LOAD_ERROR;
}

/** @brief Network staging buffer를 generic installer의 exact source로 노출한다. */
static int bundle_read(void *opaque, uint64_t offset, void *buffer,
    uint64_t size, uint64_t *transferred, uint64_t deadline_ticks)
{
    const struct MemoryBundle *bundle = opaque;
    (void)deadline_ticks;
    if (transferred != NULL) {
        *transferred = 0u;
    }
    if (bundle == NULL || buffer == NULL || transferred == NULL || size == 0u ||
        offset > bundle->size || size > bundle->size - offset) {
        return -1;
    }
    memcpy(buffer, bundle->bytes + (size_t)offset, (size_t)size);
    *transferred = size;
    return 0;
}

/** @brief D01 fixture identity를 D05 product source digest에 결합한다. */
static struct RibonUpdateManifestExpectation fixture_expectation(
    const uint8_t product_digest[32])
{
    struct RibonUpdateManifestExpectation expectation = {
        .size = sizeof(expectation),
        .abi_version = RIBON_UPDATE_MANIFEST_ABI_VERSION,
        .mode = RIBON_KEY_POLICY_MODE_RECOVERY,
        .protocol_major = 1u,
        .protocol_minor = 0u,
        .hardware_revision = 1u,
        .schema_digest = {
            0xc8u,0xddu,0x67u,0x19u,0xa4u,0x7du,0x51u,0xc6u,
            0x19u,0x2fu,0x03u,0xf4u,0x1du,0xdfu,0xeau,0xa0u,
            0xd1u,0xc9u,0x82u,0xffu,0xdau,0xc7u,0xb6u,0x06u,
            0x76u,0xa7u,0x33u,0x28u,0x6cu,0x10u,0xf0u,0xe7u,
        },
        .architecture_digest = {
            0xe8u,0x1fu,0x70u,0x88u,0x75u,0x32u,0x84u,0xe7u,
            0xbcu,0x9bu,0x9eu,0x9cu,0x36u,0xd0u,0xa1u,0x7fu,
            0x22u,0x1au,0xd2u,0xaeu,0x11u,0x64u,0xd0u,0xebu,
            0xc4u,0x75u,0x89u,0x51u,0x43u,0x46u,0x44u,0x66u,
        },
        .platform_digest = {
            0xd9u,0x65u,0x65u,0x88u,0x8fu,0x9cu,0xe4u,0x46u,
            0x66u,0xb7u,0x81u,0xa5u,0xe8u,0x59u,0xe7u,0x7fu,
            0xe1u,0x46u,0x35u,0x9fu,0x94u,0xe5u,0xa8u,0x4fu,
            0x98u,0xa9u,0xdfu,0x17u,0x5du,0x40u,0xbfu,0x82u,
        },
        .environment_digest = {
            0xf6u,0xc1u,0x9cu,0x3eu,0xd8u,0x8cu,0xc2u,0xe2u,
            0xb7u,0x4du,0xf2u,0xddu,0x40u,0xb8u,0xc3u,0xfbu,
            0xd4u,0x56u,0x97u,0x98u,0x15u,0x5fu,0x9du,0xecu,
            0x07u,0x2eu,0xdfu,0x86u,0x59u,0xc8u,0x44u,0x2fu,
        },
        .protocol_digest = {
            0x20u,0x7eu,0x27u,0xceu,0x84u,0x17u,0xfcu,0xc6u,
            0x34u,0xcau,0x38u,0xf9u,0xdfu,0xa3u,0x00u,0x83u,
            0x33u,0xffu,0xbfu,0x8eu,0x80u,0x4au,0xaau,0xb7u,
            0x23u,0xa9u,0x92u,0xbbu,0x64u,0x79u,0x58u,0x95u,
        },
        .rollback_domain_digest = {
            0xe1u,0x81u,0xd9u,0x56u,0x56u,0x01u,0x0cu,0x21u,
            0xdfu,0x57u,0x11u,0xf3u,0x02u,0x3cu,0x82u,0xe2u,
            0xdeu,0xe9u,0x11u,0xa3u,0x04u,0x85u,0xb7u,0xaeu,
            0x07u,0x0bu,0xc3u,0x9cu,0x85u,0x2cu,0x81u,0x1fu,
        },
    };
    memcpy(expectation.product_digest, product_digest, 32u);
    return expectation;
}

/** @brief 한 product-selected network object를 fixed staging buffer로 가져온다. */
static int fetch_object(
    const struct RibonRecoveryNetworkProductBinding *binding,
    const struct RibonServiceDescriptor *service,
    enum RibonRecoveryNetworkObjectKind kind,
    void *buffer,
    uint64_t capacity,
    uint64_t *size)
{
    struct RibonRecoveryNetworkResult result;
    int status = ribon_recovery_network_fetch(
        binding, service, kind, buffer, capacity, &result);
    if (status != RIBON_RECOVERY_NETWORK_STATUS_OK) {
        return status;
    }
    *size = result.bytes_received;
    return RIBON_RECOVERY_NETWORK_STATUS_OK;
}

/** @brief UEFI PXE bytes를 D01 trust와 D04 transaction에 순서대로 연결한다. */
static EFI_STATUS install_network_bundle(
    const struct RibonRecoveryNetworkProductBinding *network_binding,
    const struct RibonServiceDescriptor *network_service)
{
    struct RibonUpdateManifestExpectation expectation =
        fixture_expectation(ribon_generated_product_source_digest());
    struct MemoryBundle memory_bundle;
    struct RibonUpdateBundleSource bundle;
    struct RibonUpdateInstallRequest install;
    struct RibonUpdateTransactionJournal journal;
    struct RibonUpdateTransactionalInstallRequest request;
    struct RibonUpdateTransactionalInstallResult result;
    uint64_t manifest_size = 0u;
    uint64_t envelope_size = 0u;
    uint64_t bundle_size = 0u;
    int status;

    status = fetch_object(network_binding, network_service,
        RIBON_RECOVERY_NETWORK_OBJECT_MANIFEST,
        manifest_bytes, sizeof(manifest_bytes), &manifest_size);
    if (status != RIBON_RECOVERY_NETWORK_STATUS_OK) {
        return fail(ribon_recovery_network_status_name(status));
    }
    marker("RIBON-D05-NETWORK-MANIFEST-FETCHED");
    status = fetch_object(network_binding, network_service,
        RIBON_RECOVERY_NETWORK_OBJECT_SIGNATURE_ENVELOPE,
        envelope_bytes, sizeof(envelope_bytes), &envelope_size);
    if (status != RIBON_RECOVERY_NETWORK_STATUS_OK) {
        return fail(ribon_recovery_network_status_name(status));
    }
    marker("RIBON-D05-NETWORK-SIGNATURE-FETCHED");
    status = fetch_object(network_binding, network_service,
        RIBON_RECOVERY_NETWORK_OBJECT_BUNDLE,
        bundle_bytes, sizeof(bundle_bytes), &bundle_size);
    if (status != RIBON_RECOVERY_NETWORK_STATUS_OK) {
        return fail(ribon_recovery_network_status_name(status));
    }
    marker("RIBON-D05-NETWORK-BUNDLE-FETCHED");

    memory_bundle = (struct MemoryBundle){
        .bytes = bundle_bytes,
        .size = bundle_size,
    };
    bundle = (struct RibonUpdateBundleSource){
        .size = sizeof(bundle),
        .abi_version = RIBON_UPDATE_INSTALLER_ABI_VERSION,
        .byte_size = bundle_size,
        .context = &memory_bundle,
        .read = bundle_read,
    };
    install = (struct RibonUpdateInstallRequest){
        .size = sizeof(install),
        .abi_version = RIBON_UPDATE_INSTALLER_ABI_VERSION,
        .target_slot = 1u,
        .authorization = {
            .size = sizeof(struct RibonUpdateManifestAuthorization),
            .abi_version = RIBON_UPDATE_MANIFEST_ABI_VERSION,
            .manifest = manifest_bytes,
            .manifest_size = (size_t)manifest_size,
            .signature_envelope = envelope_bytes,
            .signature_envelope_size = (size_t)envelope_size,
            .expectation = &expectation,
            .key_policy = ribon_generated_key_policy_store(),
            .signature_provider = ribon_generated_signature_provider(),
        },
        .bundle = &bundle,
        .provider = &update_storage.provider,
        .layout = &update_storage.layout,
        .scratch = install_scratch,
        .scratch_size = sizeof(install_scratch),
        .deadline_ticks = 1u,
    };
    journal = (struct RibonUpdateTransactionJournal){
        .size = sizeof(journal),
        .abi_version = RIBON_UPDATE_TRANSACTION_ABI_VERSION,
        .provider = &update_storage.provider,
        .layout = &update_storage.layout,
        .minimum_generation = 1u,
        .deadline_ticks = 1u,
    };
    request = (struct RibonUpdateTransactionalInstallRequest){
        .size = sizeof(request),
        .abi_version = RIBON_UPDATE_TRANSACTION_ABI_VERSION,
        .pending_attempts = 3u,
        .install = &install,
        .journal = &journal,
    };
    status = ribon_update_install_transactionally(&request, &result);
    if (status != RIBON_UPDATE_TRANSACTION_STATUS_OK ||
        result.snapshot.target_slot != 1u ||
        result.snapshot.target_state != RIBON_UPDATE_SLOT_PENDING ||
        result.snapshot.metadata.active_slot != 0u ||
        result.snapshot.metadata.pending_slot != 1u ||
        result.snapshot.metadata.slots[0].state != RIBON_UPDATE_SLOT_CONFIRMED ||
        result.snapshot.metadata.slots[1].state != RIBON_UPDATE_SLOT_PENDING) {
        return fail(status == RIBON_UPDATE_TRANSACTION_STATUS_OK ?
            "transaction-result" :
            ribon_update_transaction_status_name(
                (enum RibonUpdateTransactionStatus)status));
    }
    marker("RIBON-D05-NETWORK-INSTALLED-PENDING");
    if (result.resumed_from == RIBON_UPDATE_SLOT_PENDING) {
        marker("RIBON-D05-NETWORK-REOPEN-PENDING");
    }
    return EFI_SUCCESS;
}

/** @brief D05 recovery-only UEFI application entry다. */
EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table)
{
    const struct RibonPortDescriptor *port = ribon_port_selected();
    const struct RibonPluginRegistry *registry = ribon_generated_plugin_registry();
    const struct RibonProductDescriptor *product =
        ribon_generated_product_descriptor();
    const struct RibonUpdateStorageProductBinding *storage_binding =
        ribon_generated_update_storage_binding();
    const struct RibonRecoveryNetworkProductBinding *network_binding =
        ribon_generated_recovery_network_binding();
    const struct RibonServiceDirectory *directory =
        ribon_generated_service_directory();
    const struct RibonServiceDescriptor *network_service;
    int network_status;
    struct RibonUefiAppContext native = {
        .raw_memory_map = raw_memory_map,
        .raw_memory_map_capacity = sizeof(raw_memory_map),
        .regions = memory_regions,
        .region_capacity = NETWORK_REGION_CAPACITY,
    };

    if (!ribon_port_descriptor_is_valid(port) || port->diagnostic_sink == NULL ||
        port->diagnostic_sink->operations == NULL) {
        return EFI_UNSUPPORTED;
    }
    diagnostic_sink = port->diagnostic_sink->operations;
    if (diagnostic_sink->initialize(diagnostic_sink->context) !=
        RIBON_SERVICE_STATUS_OK) {
        return EFI_DEVICE_ERROR;
    }
    marker("RIBON-D05-NETWORK-UPDATE-ENTRY");
    if (ribon_uefi_app_initialize(&native, image_handle, system_table) !=
            RIBON_UEFI_APP_STATUS_OK) {
        return fail("uefi-initialize");
    }
    if (ribon_uefi_update_storage_discover(
            &update_storage, native.boot_services, storage_binding) !=
            RIBON_UEFI_UPDATE_STORAGE_OK) {
        return fail("update-media-discovery");
    }
    marker("RIBON-D05-UPDATE-MEDIA-OPEN");
    if (ribon_plugin_registry_validate(registry, product,
            RIBON_MODE_RECOVERY) != RIBON_CORE_STATUS_OK) {
        return fail("plugin-graph");
    }
    if (ribon_service_directory_validate(directory, product,
            RIBON_MODE_RECOVERY) != RIBON_CORE_STATUS_OK) {
        return fail("service-graph");
    }
    if (ribon_protected_state_binding_validate(
            ribon_generated_protected_state_binding()) !=
            RIBON_PROTECTED_STATE_STATUS_OK) {
        return fail("protected-state-binding");
    }
    if (ribon_recovery_network_binding_validate(network_binding) !=
            RIBON_RECOVERY_NETWORK_STATUS_OK) {
        return fail("network-binding");
    }
    network_service = ribon_service_directory_find_exact(
        directory, RIBON_SERVICE_KIND_NETWORK_TRANSPORT,
        network_binding->service_id);
    if (network_service == NULL) {
        return fail("provider-discovery");
    }
    network_status = ribon_uefi_recovery_network_open(
        native.boot_services, network_binding);
    if (network_status != RIBON_UEFI_RECOVERY_NETWORK_OK) {
        switch (network_status) {
        case RIBON_UEFI_RECOVERY_NETWORK_NOT_FOUND:
            return fail("network-capability-not-found");
        case RIBON_UEFI_RECOVERY_NETWORK_AMBIGUOUS:
            return fail("network-capability-ambiguous");
        case RIBON_UEFI_RECOVERY_NETWORK_FIRMWARE_ERROR:
            return fail("network-capability-firmware-error");
        default:
            return fail("network-capability-invalid");
        }
    }
    if (ribon_uefi_recovery_network_backend() ==
            RIBON_UEFI_RECOVERY_NETWORK_BACKEND_PXE_BASE_CODE) {
        marker("RIBON-D05-UEFI-PXE-TFTP-CAPABILITY-OK");
    } else if (ribon_uefi_recovery_network_backend() ==
            RIBON_UEFI_RECOVERY_NETWORK_BACKEND_SIMPLE_NETWORK) {
        marker("RIBON-D05-UEFI-SNP-TFTP-CAPABILITY-OK");
    } else {
        return fail("network-backend-invalid");
    }
    return install_network_bundle(network_binding, network_service);
}
