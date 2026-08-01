#include <Ribon/security/protected_state.h>

static uint8_t reference_context;

static int unavailable_read(
    const struct RibonProtectedStateProvider *provider,
    const uint8_t domain[RIBON_PROTECTED_STATE_DIGEST_BYTES],
    enum RibonProtectedStateObject object,
    uint32_t slot,
    uint8_t *bytes,
    size_t size)
{
    (void)provider; (void)domain; (void)object; (void)slot; (void)bytes; (void)size;
    return RIBON_PROTECTED_STATE_PROVIDER_UNAVAILABLE;
}

static int unavailable_write(
    const struct RibonProtectedStateProvider *provider,
    const uint8_t domain[RIBON_PROTECTED_STATE_DIGEST_BYTES],
    enum RibonProtectedStateObject object,
    uint32_t slot,
    const uint8_t *bytes,
    size_t size)
{
    (void)provider; (void)domain; (void)object; (void)slot; (void)bytes; (void)size;
    return RIBON_PROTECTED_STATE_PROVIDER_UNAVAILABLE;
}

static int unavailable_flush(
    const struct RibonProtectedStateProvider *provider,
    const uint8_t domain[RIBON_PROTECTED_STATE_DIGEST_BYTES])
{
    (void)provider; (void)domain;
    return RIBON_PROTECTED_STATE_PROVIDER_UNAVAILABLE;
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
    .read = unavailable_read,
    .write = unavailable_write,
    .flush = unavailable_flush,
};
