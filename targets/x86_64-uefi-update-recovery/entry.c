#include "../../src/environments/uefi-app/uefi_app.h"
#include "../../src/environments/uefi-app/update_storage.h"

#include <Ribon/core/capability.h>
#include <Ribon/plugin/registry.h>
#include <Ribon/port/port.h>
#include <Ribon/security/protected_state.h>
#include <Ribon/update/installer.h>

#include "../../src/security/sha256.h"

#include <string.h>

#define UPDATE_MEMORY_MAP_CAPACITY (64u * 1024u)
#define UPDATE_REGION_CAPACITY 256u
#define UPDATE_MANIFEST_CAPACITY 4096u
#define UPDATE_ENVELOPE_CAPACITY 512u
#define UPDATE_BUNDLE_CAPACITY (64u * 1024u)
#define UPDATE_SCRATCH_CAPACITY (64u * 1024u)

static _Alignas(16) uint8_t raw_memory_map[UPDATE_MEMORY_MAP_CAPACITY];
static struct RibonMemoryRegion memory_regions[UPDATE_REGION_CAPACITY];
static _Alignas(4096) uint8_t manifest_bytes[UPDATE_MANIFEST_CAPACITY];
static _Alignas(4096) uint8_t envelope_bytes[UPDATE_ENVELOPE_CAPACITY];
static _Alignas(4096) uint8_t bundle_bytes[UPDATE_BUNDLE_CAPACITY];
static _Alignas(4096) uint8_t install_scratch[UPDATE_SCRATCH_CAPACITY];
static struct RibonUefiUpdateStorageContext update_storage;
static const struct RibonDiagnosticSinkServiceOperations *diagnostic_sink;

struct MemoryBundle {
    const uint8_t *bytes;
    uint64_t size;
};

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

static EFI_STATUS fail(const char *stage)
{
    marker("RIBON-D03-UPDATE-FAIL");
    marker(stage);
    return EFI_LOAD_ERROR;
}

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

static int digest_equal(const uint8_t *left, const uint8_t *right)
{
    uint8_t difference = 0u;
    uint32_t index;
    for (index = 0u; index < 32u; ++index) {
        difference |= left[index] ^ right[index];
    }
    return difference == 0u;
}

static const char *manifest_status_stage(int status)
{
    switch (status) {
    case RIBON_UPDATE_MANIFEST_STATUS_INVALID_ARGUMENT: return "auth-invalid-argument";
    case RIBON_UPDATE_MANIFEST_STATUS_CAPACITY: return "auth-capacity";
    case RIBON_UPDATE_MANIFEST_STATUS_UNSUPPORTED_VERSION: return "auth-version";
    case RIBON_UPDATE_MANIFEST_STATUS_UNSUPPORTED_ALGORITHM: return "auth-algorithm";
    case RIBON_UPDATE_MANIFEST_STATUS_MALFORMED: return "auth-malformed";
    case RIBON_UPDATE_MANIFEST_STATUS_IDENTITY_MISMATCH: return "auth-identity";
    case RIBON_UPDATE_MANIFEST_STATUS_MODE_USAGE_MISMATCH: return "auth-mode-usage";
    case RIBON_UPDATE_MANIFEST_STATUS_DOMAIN_MISMATCH: return "auth-domain";
    case RIBON_UPDATE_MANIFEST_STATUS_DIGEST_MISMATCH: return "auth-digest";
    case RIBON_UPDATE_MANIFEST_STATUS_KEY_POLICY: return "auth-key-policy";
    case RIBON_UPDATE_MANIFEST_STATUS_SIGNATURE_INVALID: return "auth-signature";
    default: return "auth-unknown";
    }
}

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

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table)
{
    const struct RibonPortDescriptor *port = ribon_port_selected();
    const struct RibonPluginRegistry *registry = ribon_generated_plugin_registry();
    const struct RibonProductDescriptor *product =
        ribon_generated_product_descriptor();
    const struct RibonUpdateStorageProductBinding *binding =
        ribon_generated_update_storage_binding();
    struct RibonUefiAppContext native = {
        .raw_memory_map = raw_memory_map,
        .raw_memory_map_capacity = sizeof(raw_memory_map),
        .regions = memory_regions,
        .region_capacity = UPDATE_REGION_CAPACITY,
    };
    struct RibonUpdateSlotMetadata metadata;
    struct RibonUpdateManifestExpectation expectation;
    struct RibonUpdateInstallResult result;
    struct MemoryBundle memory_bundle;
    struct RibonUpdateBundleSource bundle;
    struct RibonUpdateInstallRequest request;
    struct RibonUpdateManifestView authorized_manifest;
    struct RibonKeyPolicyDecision key_decision;
    uint8_t current_manifest_digest[32];
    uint64_t manifest_size = 0u;
    uint64_t envelope_size = 0u;
    uint64_t bundle_size = 0u;
    int status;

    if (!ribon_port_descriptor_is_valid(port) || port->diagnostic_sink == NULL ||
        port->diagnostic_sink->operations == NULL) {
        return EFI_UNSUPPORTED;
    }
    diagnostic_sink = port->diagnostic_sink->operations;
    if (diagnostic_sink->initialize(diagnostic_sink->context) !=
        RIBON_SERVICE_STATUS_OK) {
        return EFI_DEVICE_ERROR;
    }
    marker("RIBON-D03-UPDATE-ENTRY");
    if (ribon_uefi_app_initialize(&native, image_handle, system_table) !=
            RIBON_UEFI_APP_STATUS_OK) {
        return fail("uefi-initialize");
    }
    if (ribon_uefi_update_storage_discover(
            &update_storage, native.boot_services, binding) !=
            RIBON_UEFI_UPDATE_STORAGE_OK) {
        return fail("update-media-discovery");
    }
    marker("RIBON-D03-UPDATE-MEDIA-OPEN");
    if (ribon_plugin_registry_validate(registry, product, RIBON_MODE_RECOVERY) !=
            RIBON_CORE_STATUS_OK ||
        ribon_service_directory_validate(ribon_generated_service_directory(),
            product, RIBON_MODE_RECOVERY) != RIBON_CORE_STATUS_OK ||
        ribon_protected_state_binding_validate(
            ribon_generated_protected_state_binding()) !=
            RIBON_PROTECTED_STATE_STATUS_OK) {
        return fail("product-graph");
    }
    marker("RIBON-D03-UPDATE-PRODUCT-GRAPH-OK");
    if (ribon_uefi_app_read_file(&native, "/RIBON/UPDATE.MAN",
            manifest_bytes, sizeof(manifest_bytes), &manifest_size) !=
            RIBON_UEFI_APP_STATUS_OK ||
        ribon_uefi_app_read_file(&native, "/RIBON/UPDATE.SIG",
            envelope_bytes, sizeof(envelope_bytes), &envelope_size) !=
            RIBON_UEFI_APP_STATUS_OK ||
        ribon_uefi_app_read_file(&native, "/RIBON/UPDATE.BIN",
            bundle_bytes, sizeof(bundle_bytes), &bundle_size) !=
            RIBON_UEFI_APP_STATUS_OK) {
        return fail("esp-update-input");
    }
    if (ribon_uefi_update_storage_read_metadata(&update_storage, &metadata) !=
            RIBON_UEFI_UPDATE_STORAGE_OK) {
        return fail("metadata-open");
    }
    ribon_security_sha256(manifest_bytes, (size_t)manifest_size,
        current_manifest_digest);
    if (metadata.slots[1].state == RIBON_UPDATE_SLOT_VERIFIED) {
        if (!digest_equal(metadata.slots[1].manifest_digest,
                current_manifest_digest) ||
            !digest_equal(metadata.slots[1].layout_digest,
                update_storage.layout.identity_digest)) {
            return fail("verified-identity");
        }
        marker("RIBON-D03-UPDATE-REOPEN-VERIFIED");
        return EFI_SUCCESS;
    }
    if (metadata.active_slot != 0u ||
        metadata.slots[0].state != RIBON_UPDATE_SLOT_CONFIRMED ||
        metadata.slots[1].state != RIBON_UPDATE_SLOT_EMPTY) {
        return fail("inactive-slot-state");
    }
    expectation = fixture_expectation(ribon_generated_product_source_digest());
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
    request = (struct RibonUpdateInstallRequest){
        .size = sizeof(request),
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
        .current_metadata = &metadata,
        .scratch = install_scratch,
        .scratch_size = sizeof(install_scratch),
        .deadline_ticks = 1u,
    };
    status = ribon_update_manifest_authorize(
        &request.authorization, &authorized_manifest, &key_decision);
    if (status != RIBON_UPDATE_MANIFEST_STATUS_OK) {
        return fail(manifest_status_stage(status));
    }
    marker("RIBON-D03-UPDATE-SIGNATURE-AUTHORIZED");
    status = ribon_update_install_signed_bundle(&request, &result);
    if (status != RIBON_UPDATE_INSTALL_STATUS_OK) {
        return fail(ribon_update_install_status_name(
            (enum RibonUpdateInstallStatus)status));
    }
    if (ribon_uefi_update_storage_write_metadata(
            &update_storage, &result.verified_metadata) !=
            RIBON_UEFI_UPDATE_STORAGE_OK ||
        ribon_uefi_update_storage_read_metadata(&update_storage, &metadata) !=
            RIBON_UEFI_UPDATE_STORAGE_OK ||
        metadata.slots[1].state != RIBON_UPDATE_SLOT_VERIFIED ||
        !digest_equal(metadata.slots[1].manifest_digest,
            result.manifest_digest)) {
        return fail("metadata-verified-commit");
    }
    marker("RIBON-D03-UPDATE-INSTALLED-VERIFIED");
    return EFI_SUCCESS;
}
