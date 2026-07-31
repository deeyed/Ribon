#include <Ribon/security/signature.h>

#include <stdint.h>

/** @brief 고정 길이 ABI reserved byte가 모두 0인지 allocation 없이 검사한다. */
static int
ribon_signature_bytes_are_zero(const uint8_t *bytes, size_t size)
{
    uint8_t value = 0u;
    size_t index;

    for (index = 0u; index < size; ++index) {
        value |= bytes[index];
    }
    return value == 0u;
}

/** @brief Caller-owned workspace가 provider의 2의 거듭제곱 정렬을 만족하는지 검사한다. */
static int
ribon_signature_workspace_is_aligned(const void *workspace, size_t alignment)
{
    const uintptr_t address = (uintptr_t)workspace;

    return alignment != 0u && (alignment & (alignment - 1u)) == 0u &&
        (address & (alignment - 1u)) == 0u;
}

/**
 * @brief Immutable provider descriptor의 ABI와 bounded workspace 계약을 검사한다.
 *
 * Heap, MMIO와 persistent state를 사용하지 않으며 실패 시 provider callback을 실행하지 않는다.
 */
int
ribon_signature_provider_is_valid(
    const struct RibonSignatureProvider *provider)
{
    if (provider == NULL ||
        provider->magic != RIBON_SIGNATURE_PROVIDER_MAGIC ||
        provider->size != sizeof(*provider) ||
        provider->abi_version != RIBON_SIGNATURE_PROVIDER_ABI_VERSION ||
        (provider->provider_class !=
             RIBON_SIGNATURE_PROVIDER_CLASS_PRODUCTION &&
         provider->provider_class !=
             RIBON_SIGNATURE_PROVIDER_CLASS_FIXTURE) ||
        provider->algorithm != RIBON_SIGNATURE_ALGORITHM_ED25519 ||
        provider->flags != 0u || provider->id == NULL ||
        provider->id[0] == '\0' ||
        provider->public_key_bytes != RIBON_ED25519_PUBLIC_KEY_BYTES ||
        provider->signature_bytes != RIBON_ED25519_SIGNATURE_BYTES ||
        provider->workspace_alignment == 0u ||
        (provider->workspace_alignment &
         (provider->workspace_alignment - 1u)) != 0u ||
        provider->verify == NULL ||
        !ribon_signature_bytes_are_zero(
            (const uint8_t *)provider->reserved,
            sizeof(provider->reserved))) {
        return 0;
    }
    return 1;
}

/**
 * @brief 입력 view와 caller-owned workspace를 검증한 뒤 selected provider를 호출한다.
 *
 * 입력과 workspace pointer는 호출 동안만 빌려 주며 callback 반환 뒤 보존되지 않는다.
 * 실패는 stable `RibonSignatureStatus`로 fail closed한다.
 */
int
ribon_signature_verify(
    const struct RibonSignatureProvider *provider,
    const struct RibonSignatureVerification *request)
{
    if (!ribon_signature_provider_is_valid(provider)) {
        return RIBON_SIGNATURE_STATUS_INVALID_PROVIDER;
    }
    if (request == NULL || request->size != sizeof(*request) ||
        request->abi_version != RIBON_SIGNATURE_PROVIDER_ABI_VERSION ||
        request->flags != 0u || request->public_key == NULL ||
        request->signature == NULL ||
        (request->message == NULL && request->message_size != 0u) ||
        !ribon_signature_bytes_are_zero(
            (const uint8_t *)request->reserved,
            sizeof(request->reserved))) {
        return RIBON_SIGNATURE_STATUS_INVALID_ARGUMENT;
    }
    if (request->algorithm != provider->algorithm) {
        return RIBON_SIGNATURE_STATUS_UNSUPPORTED_ALGORITHM;
    }
    if (request->public_key_size != provider->public_key_bytes ||
        request->signature_size != provider->signature_bytes) {
        return RIBON_SIGNATURE_STATUS_INVALID_ARGUMENT;
    }
    if (provider->workspace_bytes == 0u) {
        if (request->workspace != NULL || request->workspace_size != 0u) {
            return RIBON_SIGNATURE_STATUS_INVALID_ARGUMENT;
        }
    } else if (request->workspace == NULL ||
               request->workspace_size < provider->workspace_bytes) {
        return RIBON_SIGNATURE_STATUS_WORKSPACE_TOO_SMALL;
    } else if (!ribon_signature_workspace_is_aligned(
                   request->workspace,
                   provider->workspace_alignment)) {
        return RIBON_SIGNATURE_STATUS_INVALID_ARGUMENT;
    }
    return provider->verify(provider, request);
}
