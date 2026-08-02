#include "protected_state.h"

#include <string.h>

#define PROTECTED_PAGE_BYTES 512u
#define PROTECTED_OBJECTS 2u
#define PROTECTED_SLOTS 2u
#define PROTECTED_TOTAL_PAGES (PROTECTED_OBJECTS * PROTECTED_SLOTS)

struct ReferenceContext {
    const struct RibonUpdateStorageProvider *storage;
    uint64_t base;
    uint8_t domain[RIBON_PROTECTED_STATE_DIGEST_BYTES];
};

static struct ReferenceContext reference_context;

static int bytes_equal(const uint8_t *left, const uint8_t *right, size_t size)
{
    uint8_t difference = 0u;
    size_t index;
    for (index = 0u; index < size; ++index) {
        difference |= left[index] ^ right[index];
    }
    return difference == 0u;
}

static int object_offset(
    enum RibonProtectedStateObject object,
    uint32_t slot,
    uint64_t *offset)
{
    uint32_t object_index;
    if (offset == NULL || object < RIBON_PROTECTED_STATE_OBJECT_RECORD ||
        object > RIBON_PROTECTED_STATE_OBJECT_SELECTOR ||
        slot >= PROTECTED_SLOTS || reference_context.storage == NULL) {
        return 0;
    }
    object_index = (uint32_t)object - 1u;
    *offset = reference_context.base +
        (uint64_t)(object_index * PROTECTED_SLOTS + slot) * PROTECTED_PAGE_BYTES;
    return 1;
}

static int reference_read(
    const struct RibonProtectedStateProvider *provider,
    const uint8_t domain[RIBON_PROTECTED_STATE_DIGEST_BYTES],
    enum RibonProtectedStateObject object,
    uint32_t slot,
    uint8_t *bytes,
    size_t size)
{
    uint8_t page[PROTECTED_PAGE_BYTES];
    uint64_t offset;
    uint64_t transferred = 0u;
    (void)provider;
    if (bytes == NULL || size != RIBON_PROTECTED_STATE_RECORD_BYTES ||
        !bytes_equal(domain, reference_context.domain, sizeof(reference_context.domain)) ||
        !object_offset(object, slot, &offset) ||
        reference_context.storage->read(reference_context.storage->context,
            offset, page, sizeof(page), &transferred, 1u) != 0 ||
        transferred != sizeof(page)) {
        return RIBON_PROTECTED_STATE_PROVIDER_IO_ERROR;
    }
    memcpy(bytes, page, size);
    return RIBON_PROTECTED_STATE_PROVIDER_OK;
}

static int reference_write(
    const struct RibonProtectedStateProvider *provider,
    const uint8_t domain[RIBON_PROTECTED_STATE_DIGEST_BYTES],
    enum RibonProtectedStateObject object,
    uint32_t slot,
    const uint8_t *bytes,
    size_t size)
{
    uint8_t page[PROTECTED_PAGE_BYTES] = {0};
    uint64_t offset;
    uint64_t transferred = 0u;
    (void)provider;
    if (bytes == NULL || size != RIBON_PROTECTED_STATE_RECORD_BYTES ||
        !bytes_equal(domain, reference_context.domain, sizeof(reference_context.domain)) ||
        !object_offset(object, slot, &offset)) {
        return RIBON_PROTECTED_STATE_PROVIDER_IO_ERROR;
    }
    memcpy(page, bytes, size);
    return reference_context.storage->write(reference_context.storage->context,
        offset, page, sizeof(page), &transferred, 1u) == 0 &&
        transferred == sizeof(page) ? RIBON_PROTECTED_STATE_PROVIDER_OK :
        RIBON_PROTECTED_STATE_PROVIDER_IO_ERROR;
}

static int reference_flush(
    const struct RibonProtectedStateProvider *provider,
    const uint8_t domain[RIBON_PROTECTED_STATE_DIGEST_BYTES])
{
    (void)provider;
    if (!bytes_equal(domain, reference_context.domain,
            sizeof(reference_context.domain)) || reference_context.storage == NULL) {
        return RIBON_PROTECTED_STATE_PROVIDER_IO_ERROR;
    }
    return reference_context.storage->flush(
        reference_context.storage->context, 1u) == 0 ?
        RIBON_PROTECTED_STATE_PROVIDER_OK :
        RIBON_PROTECTED_STATE_PROVIDER_IO_ERROR;
}

const struct RibonProtectedStateProvider
ribon_qemu_update_protected_state_provider_descriptor = {
    .magic = RIBON_PROTECTED_STATE_PROVIDER_MAGIC,
    .size = sizeof(ribon_qemu_update_protected_state_provider_descriptor),
    .abi_version = RIBON_PROTECTED_STATE_ABI_VERSION,
    .provider_class = RIBON_PROTECTED_STATE_PROVIDER_CLASS_REFERENCE,
    .record_slots = RIBON_PROTECTED_STATE_RECORD_SLOTS,
    .selector_slots = RIBON_PROTECTED_STATE_RECORD_SLOTS,
    .record_bytes = RIBON_PROTECTED_STATE_RECORD_BYTES,
    .selector_bytes = RIBON_PROTECTED_STATE_SELECTOR_BYTES,
    .id = "security.protected-state.reference.qemu-update",
    .context = &reference_context,
    .read = reference_read,
    .write = reference_write,
    .flush = reference_flush,
};

int
ribon_qemu_update_protected_state_bind(
    const struct RibonUpdateStorageProvider *storage,
    const struct RibonUpdateLayout *layout,
    const uint8_t domain[RIBON_PROTECTED_STATE_DIGEST_BYTES])
{
    const struct RibonUpdateLayoutRegion *region;
    uint64_t required = (uint64_t)PROTECTED_TOTAL_PAGES * PROTECTED_PAGE_BYTES;
    if (!ribon_update_storage_provider_is_valid(storage) || layout == NULL ||
        domain == NULL || storage->read_alignment > PROTECTED_PAGE_BYTES ||
        storage->write_alignment > PROTECTED_PAGE_BYTES) {
        return 0;
    }
    region = &layout->regions[RIBON_UPDATE_REGION_TRAILING_RESERVED - 1u];
    if (region->kind != RIBON_UPDATE_REGION_TRAILING_RESERVED ||
        region->length < required ||
        (region->offset & (PROTECTED_PAGE_BYTES - 1u)) != 0u) {
        return 0;
    }
    memset(&reference_context, 0, sizeof(reference_context));
    reference_context.storage = storage;
    reference_context.base = region->offset;
    memcpy(reference_context.domain, domain, sizeof(reference_context.domain));
    return 1;
}
