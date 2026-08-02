#include <Ribon/update/confirmation.h>

#include "../security/sha256.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const uint8_t envelope_magic[32] =
    "RIBON-BOOT-CONFIRM-ENV-V1";
static const uint8_t binding_domain[32] =
    "RIBON-BOOT-ATTEMPT-BIND-V1";

static uint16_t
load_u16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t
load_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t
load_u64(const uint8_t *bytes)
{
    return (uint64_t)load_u32(bytes) |
        ((uint64_t)load_u32(bytes + 4u) << 32);
}

static void
store_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void
store_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void
store_u64(uint8_t *bytes, uint64_t value)
{
    store_u32(bytes, (uint32_t)value);
    store_u32(bytes + 4u, (uint32_t)(value >> 32));
}

static int
bytes_zero(const void *pointer, size_t size)
{
    const uint8_t *bytes = pointer;
    uint8_t value = 0u;
    size_t index;

    for (index = 0u; index < size; ++index) {
        value |= bytes[index];
    }
    return value == 0u;
}

static int
bytes_equal(const void *left_pointer, const void *right_pointer, size_t size)
{
    const uint8_t *left = left_pointer;
    const uint8_t *right = right_pointer;
    uint8_t difference = 0u;
    size_t index;

    for (index = 0u; index < size; ++index) {
        difference |= left[index] ^ right[index];
    }
    return difference == 0u;
}

static int
id_is_valid(const uint8_t *bytes, size_t size, size_t maximum)
{
    size_t index;

    if (bytes == NULL || size == 0u || size > maximum) {
        return 0;
    }
    for (index = 0u; index < size; ++index) {
        if (bytes[index] == 0u) {
            return 0;
        }
    }
    return 1;
}

static int
add_size(size_t left, size_t right, size_t *result)
{
    if (result == NULL || left > SIZE_MAX - right) {
        return 0;
    }
    *result = left + right;
    return 1;
}

static int
identity_is_valid(const struct RibonBootAttemptIdentity *identity)
{
    return identity != NULL && identity->size == sizeof(*identity) &&
        identity->abi_version == RIBON_BOOT_CONFIRMATION_ABI_VERSION &&
        identity->mode >= RIBON_KEY_POLICY_MODE_NORMAL &&
        identity->mode <= RIBON_KEY_POLICY_MODE_DIAGNOSTIC &&
        identity->slot_id < RIBON_UPDATE_SLOT_COUNT &&
        identity->protocol_major != 0u && identity->policy_version != 0u &&
        identity->flags == 0u && identity->image_generation != 0u &&
        identity->manifest_sequence != 0u &&
        id_is_valid(identity->product_id, identity->product_id_size,
                    RIBON_BOOT_CONFIRMATION_MAX_PRODUCT_ID_BYTES) &&
        id_is_valid(identity->protocol_id, identity->protocol_id_size,
                    RIBON_BOOT_CONFIRMATION_MAX_PROTOCOL_ID_BYTES) &&
        !bytes_zero(identity->manifest_digest, sizeof(identity->manifest_digest)) &&
        !bytes_zero(identity->product_digest, sizeof(identity->product_digest)) &&
        !bytes_zero(identity->rollback_domain_digest,
                    sizeof(identity->rollback_domain_digest)) &&
        bytes_zero(identity->reserved, sizeof(identity->reserved));
}

static int
nonce_source_is_valid(const struct RibonBootAttemptNonceSource *source)
{
    return source != NULL && source->size == sizeof(*source) &&
        source->abi_version == RIBON_BOOT_CONFIRMATION_ABI_VERSION &&
        source->flags == 0u && source->reserved0 == 0u && source->fill != NULL &&
        bytes_zero(source->reserved, sizeof(source->reserved));
}

int
ribon_boot_confirmation_envelope_encode(
    const struct RibonBootConfirmationEnvelopeSource *source,
    uint8_t *output,
    size_t capacity,
    size_t *written)
{
    size_t product_offset = RIBON_BOOT_CONFIRMATION_HEADER_BYTES;
    size_t protocol_offset;
    size_t key_offset;
    size_t health_offset;
    size_t signature_offset;
    size_t total;
    uint8_t health_digest[32];

    if (written != NULL) {
        *written = 0u;
    }
    if (source == NULL || output == NULL || written == NULL ||
        source->size != sizeof(*source) ||
        source->abi_version != RIBON_BOOT_CONFIRMATION_ABI_VERSION ||
        source->flags != 0u || source->reserved0 != 0u ||
        source->slot_id >= RIBON_UPDATE_SLOT_COUNT ||
        source->protocol_major == 0u || source->policy_version == 0u ||
        source->image_generation == 0u || source->manifest_sequence == 0u ||
        source->attempt_sequence == 0u ||
        !id_is_valid(source->product_id, source->product_id_size,
                     RIBON_BOOT_CONFIRMATION_MAX_PRODUCT_ID_BYTES) ||
        !id_is_valid(source->protocol_id, source->protocol_id_size,
                     RIBON_BOOT_CONFIRMATION_MAX_PROTOCOL_ID_BYTES) ||
        !id_is_valid(source->key_id, source->key_id_size,
                     RIBON_BOOT_CONFIRMATION_MAX_KEY_ID_BYTES) ||
        source->health_payload == NULL || source->health_payload_size == 0u ||
        source->health_payload_size > RIBON_BOOT_CONFIRMATION_MAX_HEALTH_BYTES ||
        bytes_zero(source->manifest_digest, sizeof(source->manifest_digest)) ||
        bytes_zero(source->nonce, sizeof(source->nonce)) ||
        !bytes_zero(source->reserved, sizeof(source->reserved)) ||
        !add_size(product_offset, source->product_id_size, &protocol_offset) ||
        !add_size(protocol_offset, source->protocol_id_size, &key_offset) ||
        !add_size(key_offset, source->key_id_size, &health_offset) ||
        !add_size(health_offset, source->health_payload_size, &signature_offset) ||
        !add_size(signature_offset, RIBON_ED25519_SIGNATURE_BYTES, &total) ||
        total > capacity || total > RIBON_BOOT_CONFIRMATION_MAX_WIRE_BYTES ||
        product_offset > UINT32_MAX || protocol_offset > UINT32_MAX ||
        key_offset > UINT32_MAX || health_offset > UINT32_MAX ||
        signature_offset > UINT32_MAX || total > UINT32_MAX ||
        source->product_id_size > UINT16_MAX ||
        source->protocol_id_size > UINT16_MAX || source->key_id_size > UINT16_MAX ||
        source->health_payload_size > UINT32_MAX) {
        return RIBON_BOOT_CONFIRMATION_STATUS_INVALID_ARGUMENT;
    }
    memset(output, 0, total);
    memcpy(output, envelope_magic, sizeof(envelope_magic));
    store_u16(output + 32u, RIBON_BOOT_CONFIRMATION_ABI_VERSION);
    store_u16(output + 34u, 0u);
    store_u32(output + 36u, RIBON_BOOT_CONFIRMATION_HEADER_BYTES);
    store_u32(output + 40u, (uint32_t)total);
    store_u32(output + 44u, source->slot_id);
    store_u32(output + 48u, source->protocol_major);
    store_u32(output + 52u, source->protocol_minor);
    store_u32(output + 56u, source->policy_version);
    store_u64(output + 64u, source->image_generation);
    store_u64(output + 72u, source->manifest_sequence);
    store_u64(output + 80u, source->attempt_sequence);
    store_u16(output + 88u, (uint16_t)source->product_id_size);
    store_u16(output + 90u, (uint16_t)source->protocol_id_size);
    store_u16(output + 92u, (uint16_t)source->key_id_size);
    store_u32(output + 96u, (uint32_t)source->health_payload_size);
    store_u32(output + 100u, (uint32_t)product_offset);
    store_u32(output + 104u, (uint32_t)protocol_offset);
    store_u32(output + 108u, (uint32_t)key_offset);
    store_u32(output + 112u, (uint32_t)health_offset);
    store_u32(output + 116u, (uint32_t)signature_offset);
    store_u32(output + 120u, RIBON_ED25519_SIGNATURE_BYTES);
    memcpy(output + 128u, source->manifest_digest, 32u);
    memcpy(output + 160u, source->nonce, sizeof(source->nonce));
    ribon_security_sha256(source->health_payload,
                          source->health_payload_size, health_digest);
    memcpy(output + 192u, health_digest, sizeof(health_digest));
    memcpy(output + product_offset, source->product_id, source->product_id_size);
    memcpy(output + protocol_offset, source->protocol_id, source->protocol_id_size);
    memcpy(output + key_offset, source->key_id, source->key_id_size);
    memcpy(output + health_offset, source->health_payload,
           source->health_payload_size);
    memcpy(output + signature_offset, source->signature,
           RIBON_ED25519_SIGNATURE_BYTES);
    *written = total;
    return RIBON_BOOT_CONFIRMATION_STATUS_OK;
}

int
ribon_boot_confirmation_envelope_open(
    const uint8_t *bytes,
    size_t size,
    struct RibonBootConfirmationEnvelopeView *view)
{
    uint8_t health_digest[32];
    size_t product_size;
    size_t protocol_size;
    size_t key_size;
    size_t health_size;
    size_t product_offset;
    size_t protocol_offset;
    size_t key_offset;
    size_t health_offset;
    size_t signature_offset;
    size_t signature_size;
    size_t expected;

    if (view != NULL) {
        memset(view, 0, sizeof(*view));
    }
    if (bytes == NULL || view == NULL ||
        size < RIBON_BOOT_CONFIRMATION_HEADER_BYTES ||
        size > RIBON_BOOT_CONFIRMATION_MAX_WIRE_BYTES) {
        return RIBON_BOOT_CONFIRMATION_STATUS_MALFORMED;
    }
    product_size = load_u16(bytes + 88u);
    protocol_size = load_u16(bytes + 90u);
    key_size = load_u16(bytes + 92u);
    health_size = load_u32(bytes + 96u);
    product_offset = load_u32(bytes + 100u);
    protocol_offset = load_u32(bytes + 104u);
    key_offset = load_u32(bytes + 108u);
    health_offset = load_u32(bytes + 112u);
    signature_offset = load_u32(bytes + 116u);
    signature_size = load_u32(bytes + 120u);
    if (
        !bytes_equal(bytes, envelope_magic, sizeof(envelope_magic)) ||
        load_u16(bytes + 32u) != RIBON_BOOT_CONFIRMATION_ABI_VERSION ||
        load_u16(bytes + 34u) != 0u ||
        load_u32(bytes + 36u) != RIBON_BOOT_CONFIRMATION_HEADER_BYTES ||
        load_u32(bytes + 40u) != size ||
        load_u32(bytes + 44u) >= RIBON_UPDATE_SLOT_COUNT ||
        load_u32(bytes + 48u) == 0u || load_u32(bytes + 56u) == 0u ||
        load_u32(bytes + 60u) != 0u || load_u64(bytes + 64u) == 0u ||
        load_u64(bytes + 72u) == 0u || load_u64(bytes + 80u) == 0u ||
        load_u16(bytes + 94u) != 0u || load_u32(bytes + 124u) != 0u ||
        bytes_zero(bytes + 128u, 32u) || bytes_zero(bytes + 160u, 32u) ||
        bytes_zero(bytes + 192u, 32u) || !bytes_zero(bytes + 224u, 32u) ||
        product_size == 0u ||
        product_size > RIBON_BOOT_CONFIRMATION_MAX_PRODUCT_ID_BYTES ||
        protocol_size == 0u ||
        protocol_size > RIBON_BOOT_CONFIRMATION_MAX_PROTOCOL_ID_BYTES ||
        key_size == 0u || key_size > RIBON_BOOT_CONFIRMATION_MAX_KEY_ID_BYTES ||
        health_size == 0u || health_size > RIBON_BOOT_CONFIRMATION_MAX_HEALTH_BYTES ||
        signature_size != RIBON_ED25519_SIGNATURE_BYTES ||
        product_offset != RIBON_BOOT_CONFIRMATION_HEADER_BYTES ||
        !add_size(product_offset, product_size, &expected) ||
        protocol_offset != expected ||
        !add_size(protocol_offset, protocol_size, &expected) ||
        key_offset != expected || !add_size(key_offset, key_size, &expected) ||
        health_offset != expected ||
        !add_size(health_offset, health_size, &expected) ||
        signature_offset != expected ||
        !add_size(signature_offset, signature_size, &expected) ||
        expected != size) {
        return RIBON_BOOT_CONFIRMATION_STATUS_MALFORMED;
    }
    if (!id_is_valid(bytes + product_offset, product_size,
                     RIBON_BOOT_CONFIRMATION_MAX_PRODUCT_ID_BYTES) ||
        !id_is_valid(bytes + protocol_offset, protocol_size,
                     RIBON_BOOT_CONFIRMATION_MAX_PROTOCOL_ID_BYTES) ||
        !id_is_valid(bytes + key_offset, key_size,
                     RIBON_BOOT_CONFIRMATION_MAX_KEY_ID_BYTES)) {
        return RIBON_BOOT_CONFIRMATION_STATUS_MALFORMED;
    }
    ribon_security_sha256(bytes + health_offset, health_size, health_digest);
    if (!bytes_equal(health_digest, bytes + 192u, sizeof(health_digest))) {
        return RIBON_BOOT_CONFIRMATION_STATUS_MALFORMED;
    }
    *view = (struct RibonBootConfirmationEnvelopeView){
        .size = sizeof(*view),
        .abi_version = RIBON_BOOT_CONFIRMATION_ABI_VERSION,
        .slot_id = load_u32(bytes + 44u),
        .protocol_major = load_u32(bytes + 48u),
        .protocol_minor = load_u32(bytes + 52u),
        .policy_version = load_u32(bytes + 56u),
        .image_generation = load_u64(bytes + 64u),
        .manifest_sequence = load_u64(bytes + 72u),
        .attempt_sequence = load_u64(bytes + 80u),
        .product_id = bytes + product_offset,
        .product_id_size = product_size,
        .protocol_id = bytes + protocol_offset,
        .protocol_id_size = protocol_size,
        .key_id = bytes + key_offset,
        .key_id_size = key_size,
        .health_payload = bytes + health_offset,
        .health_payload_size = health_size,
        .signature = bytes + signature_offset,
        .signature_size = signature_size,
        .authenticated_message = bytes,
        .authenticated_message_size = signature_offset,
    };
    memcpy(view->manifest_digest, bytes + 128u, 32u);
    memcpy(view->nonce, bytes + 160u, 32u);
    memcpy(view->health_payload_digest, bytes + 192u, 32u);
    return RIBON_BOOT_CONFIRMATION_STATUS_OK;
}

int
ribon_boot_confirmation_binding_digest(
    const struct RibonBootAttemptIdentity *identity,
    const uint8_t nonce[RIBON_BOOT_CONFIRMATION_NONCE_BYTES],
    uint64_t attempt_sequence,
    uint8_t digest[32])
{
    uint8_t bytes[512];
    size_t offset = 0u;

    if (!identity_is_valid(identity) || nonce == NULL ||
        bytes_zero(nonce, RIBON_BOOT_CONFIRMATION_NONCE_BYTES) ||
        attempt_sequence == 0u || digest == NULL) {
        return RIBON_BOOT_CONFIRMATION_STATUS_INVALID_ARGUMENT;
    }
    memset(bytes, 0, sizeof(bytes));
    memcpy(bytes + offset, binding_domain, sizeof(binding_domain));
    offset += sizeof(binding_domain);
    store_u32(bytes + offset, RIBON_BOOT_CONFIRMATION_ABI_VERSION); offset += 4u;
    store_u32(bytes + offset, (uint32_t)identity->mode); offset += 4u;
    store_u32(bytes + offset, identity->slot_id); offset += 4u;
    store_u32(bytes + offset, identity->protocol_major); offset += 4u;
    store_u32(bytes + offset, identity->protocol_minor); offset += 4u;
    store_u32(bytes + offset, identity->policy_version); offset += 4u;
    store_u64(bytes + offset, identity->image_generation); offset += 8u;
    store_u64(bytes + offset, identity->manifest_sequence); offset += 8u;
    store_u64(bytes + offset, attempt_sequence); offset += 8u;
    store_u32(bytes + offset, (uint32_t)identity->product_id_size); offset += 4u;
    store_u32(bytes + offset, (uint32_t)identity->protocol_id_size); offset += 4u;
    memcpy(bytes + offset, identity->manifest_digest, 32u); offset += 32u;
    memcpy(bytes + offset, identity->product_digest, 32u); offset += 32u;
    memcpy(bytes + offset, identity->rollback_domain_digest, 32u); offset += 32u;
    memcpy(bytes + offset, nonce, RIBON_BOOT_CONFIRMATION_NONCE_BYTES);
    offset += RIBON_BOOT_CONFIRMATION_NONCE_BYTES;
    memcpy(bytes + offset, identity->product_id, identity->product_id_size);
    offset += identity->product_id_size;
    memcpy(bytes + offset, identity->protocol_id, identity->protocol_id_size);
    offset += identity->protocol_id_size;
    ribon_security_sha256(bytes, offset, digest);
    return RIBON_BOOT_CONFIRMATION_STATUS_OK;
}

static int
pending_identity_matches(
    const struct RibonUpdateTransactionSnapshot *snapshot,
    const struct RibonBootAttemptIdentity *identity)
{
    const struct RibonUpdateSlotEntry *entry;

    if (snapshot->metadata.pending_slot != identity->slot_id ||
        snapshot->target_slot != identity->slot_id ||
        snapshot->target_state != RIBON_UPDATE_SLOT_PENDING) {
        return 0;
    }
    entry = &snapshot->metadata.slots[identity->slot_id];
    return entry->state == RIBON_UPDATE_SLOT_PENDING &&
        entry->image_generation == identity->image_generation &&
        bytes_equal(entry->manifest_digest, identity->manifest_digest, 32u);
}

int
ribon_boot_confirmation_begin_attempt(
    const struct RibonBootAttemptBeginRequest *request,
    struct RibonBootAttempt *attempt)
{
    struct RibonUpdateTransactionSnapshot update;
    struct RibonProtectedStateSnapshot protected;
    uint8_t nonce[RIBON_BOOT_CONFIRMATION_NONCE_BYTES];
    uint8_t binding[32];
    uint64_t attempt_sequence;
    int status;

    if (attempt != NULL) {
        memset(attempt, 0, sizeof(*attempt));
    }
    if (request == NULL || attempt == NULL ||
        request->size != sizeof(*request) ||
        request->abi_version != RIBON_BOOT_CONFIRMATION_ABI_VERSION ||
        request->flags != 0u || request->maximum_attempts == 0u ||
        request->maximum_attempts > RIBON_PROTECTED_STATE_MAX_TRIAL_ATTEMPTS ||
        !identity_is_valid(request->identity) || request->update_journal == NULL ||
        request->protected_journal == NULL ||
        !nonce_source_is_valid(request->nonce_source) ||
        !bytes_zero(request->reserved, sizeof(request->reserved))) {
        return RIBON_BOOT_CONFIRMATION_STATUS_INVALID_ARGUMENT;
    }
    status = ribon_update_transaction_open(request->update_journal, &update);
    if (status != RIBON_UPDATE_TRANSACTION_STATUS_OK ||
        !pending_identity_matches(&update, request->identity) ||
        update.metadata.slots[request->identity->slot_id].boot_attempts !=
            request->maximum_attempts) {
        return RIBON_BOOT_CONFIRMATION_STATUS_UPDATE_STATE;
    }
    status = ribon_protected_state_open(request->protected_journal, &protected);
    if (status != RIBON_PROTECTED_STATE_STATUS_OK) {
        return RIBON_BOOT_CONFIRMATION_STATUS_PROTECTED_STATE;
    }
    if (protected.attempt_sequence == UINT64_MAX) {
        return RIBON_BOOT_CONFIRMATION_STATUS_OVERFLOW;
    }
    attempt_sequence = protected.attempt_sequence + 1u;
    if (request->nonce_source->fill(request->nonce_source->context, nonce) != 0 ||
        bytes_zero(nonce, sizeof(nonce)) ||
        ribon_boot_confirmation_binding_digest(request->identity, nonce,
            attempt_sequence, binding) != RIBON_BOOT_CONFIRMATION_STATUS_OK) {
        return RIBON_BOOT_CONFIRMATION_STATUS_INVALID_ARGUMENT;
    }
    if (protected.kind == RIBON_PROTECTED_STATE_KIND_CONFIRMED) {
        status = ribon_protected_state_begin_bound_trial(
            request->protected_journal, request->identity->manifest_sequence,
            request->maximum_attempts, binding, attempt_sequence, &protected);
    } else if (protected.kind == RIBON_PROTECTED_STATE_KIND_TRIAL &&
               protected.pending_sequence == request->identity->manifest_sequence) {
        status = ribon_protected_state_rebind_trial_attempt(
            request->protected_journal, request->identity->manifest_sequence,
            binding, attempt_sequence, &protected);
    } else {
        return RIBON_BOOT_CONFIRMATION_STATUS_STALE;
    }
    if (status == RIBON_PROTECTED_STATE_STATUS_ATTEMPTS_EXHAUSTED) {
        return RIBON_BOOT_CONFIRMATION_STATUS_ATTEMPTS_EXHAUSTED;
    }
    if (status != RIBON_PROTECTED_STATE_STATUS_OK) {
        return RIBON_BOOT_CONFIRMATION_STATUS_PROTECTED_STATE;
    }
    status = ribon_protected_state_consume_trial_attempt(
        request->protected_journal, request->identity->manifest_sequence, &protected);
    if (status == RIBON_PROTECTED_STATE_STATUS_ATTEMPTS_EXHAUSTED) {
        return RIBON_BOOT_CONFIRMATION_STATUS_ATTEMPTS_EXHAUSTED;
    }
    if (status != RIBON_PROTECTED_STATE_STATUS_OK ||
        !bytes_equal(protected.trial_binding_digest, binding, sizeof(binding)) ||
        protected.attempt_sequence != attempt_sequence) {
        return RIBON_BOOT_CONFIRMATION_STATUS_PROTECTED_STATE;
    }
    *attempt = (struct RibonBootAttempt){
        .size = sizeof(*attempt),
        .abi_version = RIBON_BOOT_CONFIRMATION_ABI_VERSION,
        .slot_id = request->identity->slot_id,
        .attempts_remaining = protected.trial_attempts_remaining,
        .image_generation = request->identity->image_generation,
        .manifest_sequence = request->identity->manifest_sequence,
        .attempt_sequence = attempt_sequence,
        .protected_generation = protected.generation,
    };
    memcpy(attempt->manifest_digest, request->identity->manifest_digest, 32u);
    memcpy(attempt->nonce, nonce, sizeof(attempt->nonce));
    memcpy(attempt->binding_digest, binding, sizeof(attempt->binding_digest));
    return RIBON_BOOT_CONFIRMATION_STATUS_OK;
}

static int
view_matches_identity(
    const struct RibonBootConfirmationEnvelopeView *view,
    const struct RibonBootAttemptIdentity *identity)
{
    return view->slot_id == identity->slot_id &&
        view->protocol_major == identity->protocol_major &&
        view->protocol_minor == identity->protocol_minor &&
        view->policy_version == identity->policy_version &&
        view->image_generation == identity->image_generation &&
        view->manifest_sequence == identity->manifest_sequence &&
        view->product_id_size == identity->product_id_size &&
        view->protocol_id_size == identity->protocol_id_size &&
        bytes_equal(view->product_id, identity->product_id,
                    identity->product_id_size) &&
        bytes_equal(view->protocol_id, identity->protocol_id,
                    identity->protocol_id_size) &&
        bytes_equal(view->manifest_digest, identity->manifest_digest, 32u);
}

static int
protocol_matches_identity(
    const struct RibonBootProtocol *protocol,
    const struct RibonBootAttemptIdentity *identity)
{
    size_t size = 0u;

    if (!ribon_boot_protocol_is_valid(protocol) ||
        protocol->abi_version != identity->protocol_major ||
        identity->protocol_minor != 0u) {
        return 0;
    }
    while (size < RIBON_BOOT_CONFIRMATION_MAX_PROTOCOL_ID_BYTES &&
           protocol->id[size] != '\0') {
        ++size;
    }
    return size < RIBON_BOOT_CONFIRMATION_MAX_PROTOCOL_ID_BYTES &&
        size == identity->protocol_id_size &&
        bytes_equal(protocol->id, identity->protocol_id, size);
}

int
ribon_boot_confirmation_accept(
    const struct RibonBootConfirmationAcceptRequest *request,
    struct RibonBootConfirmationReceipt *receipt)
{
    struct RibonBootConfirmationEnvelopeView view;
    struct RibonProtectedStateSnapshot protected;
    struct RibonUpdateTransactionSnapshot update;
    struct RibonUpdateConfirmPendingRequest confirm_request = {0};
    struct RibonUpdateConfirmPendingResult confirm_result;
    struct RibonKeyPolicyRequest policy = {0};
    struct RibonKeyPolicySignatureVerification verification = {0};
    struct RibonKeyPolicyDecision decision;
    struct RibonBootHealthPayload health = {0};
    uint8_t binding[32];
    int update_was_confirmed;
    int status;

    if (receipt != NULL) {
        memset(receipt, 0, sizeof(*receipt));
    }
    if (request == NULL || receipt == NULL ||
        request->size != sizeof(*request) ||
        request->abi_version != RIBON_BOOT_CONFIRMATION_ABI_VERSION ||
        request->flags != 0u || request->reserved0 != 0u ||
        request->envelope == NULL || request->envelope_size == 0u ||
        !identity_is_valid(request->identity) || request->protocol == NULL ||
        request->key_policy == NULL || request->signature_provider == NULL ||
        request->update_journal == NULL || request->protected_journal == NULL ||
        !bytes_zero(request->reserved, sizeof(request->reserved))) {
        return RIBON_BOOT_CONFIRMATION_STATUS_INVALID_ARGUMENT;
    }
    status = ribon_boot_confirmation_envelope_open(
        request->envelope, request->envelope_size, &view);
    if (status != RIBON_BOOT_CONFIRMATION_STATUS_OK) {
        return status;
    }
    if (!view_matches_identity(&view, request->identity) ||
        !protocol_matches_identity(request->protocol, request->identity) ||
        ribon_boot_confirmation_binding_digest(request->identity, view.nonce,
            view.attempt_sequence, binding) != RIBON_BOOT_CONFIRMATION_STATUS_OK) {
        return RIBON_BOOT_CONFIRMATION_STATUS_IDENTITY;
    }
    status = ribon_update_transaction_open(request->update_journal, &update);
    if (status != RIBON_UPDATE_TRANSACTION_STATUS_OK) {
        return RIBON_BOOT_CONFIRMATION_STATUS_UPDATE_STATE;
    }
    update_was_confirmed =
        update.metadata.active_slot == request->identity->slot_id &&
        update.metadata.pending_slot == RIBON_UPDATE_SLOT_NONE &&
        update.metadata.slots[request->identity->slot_id].state ==
            RIBON_UPDATE_SLOT_CONFIRMED &&
        update.metadata.slots[request->identity->slot_id].image_generation ==
            request->identity->image_generation &&
        bytes_equal(update.metadata.slots[request->identity->slot_id].manifest_digest,
                    request->identity->manifest_digest, 32u);
    if (!update_was_confirmed && !pending_identity_matches(&update, request->identity)) {
        return RIBON_BOOT_CONFIRMATION_STATUS_IDENTITY;
    }
    status = ribon_protected_state_open(request->protected_journal, &protected);
    if (status != RIBON_PROTECTED_STATE_STATUS_OK ||
        protected.attempt_sequence != view.attempt_sequence ||
        !bytes_equal(protected.trial_binding_digest, binding, sizeof(binding)) ||
        !((protected.kind == RIBON_PROTECTED_STATE_KIND_TRIAL &&
           protected.pending_sequence == request->identity->manifest_sequence) ||
          (protected.kind == RIBON_PROTECTED_STATE_KIND_CONFIRMED &&
           protected.confirmed_floor == request->identity->manifest_sequence))) {
        return RIBON_BOOT_CONFIRMATION_STATUS_STALE;
    }
    policy.size = sizeof(policy);
    policy.abi_version = RIBON_KEY_POLICY_ABI_VERSION;
    policy.mode = request->identity->mode;
    policy.usage = RIBON_KEY_POLICY_USAGE_BOOT_CONFIRMATION;
    policy.key_id = view.key_id;
    policy.key_id_size = view.key_id_size;
    policy.sequence = view.manifest_sequence;
    memcpy(policy.product_digest, request->identity->product_digest, 32u);
    memcpy(policy.rollback_domain_digest,
           request->identity->rollback_domain_digest, 32u);
    verification.size = sizeof(verification);
    verification.abi_version = RIBON_KEY_POLICY_ABI_VERSION;
    verification.policy = &policy;
    verification.provider = request->signature_provider;
    verification.message = view.authenticated_message;
    verification.message_size = view.authenticated_message_size;
    verification.signature = view.signature;
    verification.signature_size = view.signature_size;
    verification.workspace = request->signature_workspace;
    verification.workspace_size = request->signature_workspace_size;
    if (ribon_key_policy_verify(request->key_policy, &verification, &decision) !=
            RIBON_KEY_POLICY_STATUS_OK) {
        return RIBON_BOOT_CONFIRMATION_STATUS_AUTHENTICATOR;
    }
    health.size = sizeof(health);
    health.abi_version = RIBON_BOOT_CONFIRMATION_ABI_VERSION;
    health.bytes = view.health_payload;
    health.byte_size = view.health_payload_size;
    memcpy(health.digest, view.health_payload_digest, sizeof(health.digest));
    if (ribon_boot_protocol_validate_boot_health(request->protocol, &health) !=
            RIBON_PROTOCOL_STATUS_OK) {
        return RIBON_BOOT_CONFIRMATION_STATUS_HEALTH_REJECTED;
    }
    status = ribon_protected_state_confirm_bound(
        request->protected_journal, request->identity->manifest_sequence,
        binding, view.attempt_sequence, &protected);
    if (status != RIBON_PROTECTED_STATE_STATUS_OK) {
        return RIBON_BOOT_CONFIRMATION_STATUS_PROTECTED_STATE;
    }
    confirm_request.size = sizeof(confirm_request);
    confirm_request.abi_version = RIBON_UPDATE_TRANSACTION_ABI_VERSION;
    confirm_request.target_slot = request->identity->slot_id;
    confirm_request.image_generation = request->identity->image_generation;
    memcpy(confirm_request.manifest_digest,
           request->identity->manifest_digest, 32u);
    confirm_request.journal = request->update_journal;
    confirm_request.observer = request->observer;
    status = ribon_update_transaction_confirm_pending(
        &confirm_request, &confirm_result);
    if (status != RIBON_UPDATE_TRANSACTION_STATUS_OK) {
        return RIBON_BOOT_CONFIRMATION_STATUS_UPDATE_STATE;
    }
    *receipt = (struct RibonBootConfirmationReceipt){
        .size = sizeof(*receipt),
        .abi_version = RIBON_BOOT_CONFIRMATION_ABI_VERSION,
        .status = (update_was_confirmed || confirm_result.duplicate != 0u) ?
            RIBON_BOOT_CONFIRMATION_STATUS_DUPLICATE :
            RIBON_BOOT_CONFIRMATION_STATUS_OK,
        .duplicate = (update_was_confirmed || confirm_result.duplicate != 0u),
        .attempt_sequence = view.attempt_sequence,
        .protected_generation = protected.generation,
        .update_generation = confirm_result.snapshot.journal_generation,
        .active_slot = confirm_result.snapshot.metadata.active_slot,
    };
    return receipt->status;
}

const char *
ribon_boot_confirmation_status_name(enum RibonBootConfirmationStatus status)
{
    switch (status) {
    case RIBON_BOOT_CONFIRMATION_STATUS_OK: return "ok";
    case RIBON_BOOT_CONFIRMATION_STATUS_DUPLICATE: return "duplicate";
    case RIBON_BOOT_CONFIRMATION_STATUS_INVALID_ARGUMENT: return "invalid-argument";
    case RIBON_BOOT_CONFIRMATION_STATUS_MALFORMED: return "malformed";
    case RIBON_BOOT_CONFIRMATION_STATUS_IDENTITY: return "identity";
    case RIBON_BOOT_CONFIRMATION_STATUS_STALE: return "stale";
    case RIBON_BOOT_CONFIRMATION_STATUS_AUTHENTICATOR: return "authenticator";
    case RIBON_BOOT_CONFIRMATION_STATUS_HEALTH_REJECTED: return "health-rejected";
    case RIBON_BOOT_CONFIRMATION_STATUS_PROTECTED_STATE: return "protected-state";
    case RIBON_BOOT_CONFIRMATION_STATUS_UPDATE_STATE: return "update-state";
    case RIBON_BOOT_CONFIRMATION_STATUS_ATTEMPTS_EXHAUSTED: return "attempts-exhausted";
    case RIBON_BOOT_CONFIRMATION_STATUS_OVERFLOW: return "overflow";
    default: return "unknown";
    }
}
