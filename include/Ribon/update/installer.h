#ifndef RIBON_UPDATE_INSTALLER_H
#define RIBON_UPDATE_INSTALLER_H

#include <stddef.h>
#include <stdint.h>

#include <Ribon/update/manifest.h>
#include <Ribon/update/storage.h>

/** @brief Allocation-free signed-bundle installer native ABI다. */
#define RIBON_UPDATE_INSTALLER_ABI_VERSION 1u

struct RibonUpdateTransactionObserver;

/** @brief Bundle source가 exact byte range를 caller buffer로 복사하는 callback이다. */
typedef int (*RibonUpdateBundleReadFn)(
    void *context,
    uint64_t offset,
    void *buffer,
    uint64_t size,
    uint64_t *transferred,
    uint64_t deadline_ticks);

/** @brief OS와 firmware transport에 독립적인 bounded bundle byte source다. */
struct RibonUpdateBundleSource {
    uint32_t size;
    uint32_t abi_version;
    uint32_t flags;
    uint32_t reserved0;
    uint64_t byte_size;
    void *context;
    RibonUpdateBundleReadFn read;
    uint64_t reserved[4];
};

/** @brief Signed manifest에서 inactive slot VERIFIED 상태까지의 install request다. */
struct RibonUpdateInstallRequest {
    uint32_t size;
    uint32_t abi_version;
    uint32_t flags;
    uint32_t target_slot;
    struct RibonUpdateManifestAuthorization authorization;
    const struct RibonUpdateBundleSource *bundle;
    const struct RibonUpdateStorageProvider *provider;
    const struct RibonUpdateLayout *layout;
    const struct RibonUpdateSlotMetadata *current_metadata;
    void *scratch;
    size_t scratch_size;
    uint64_t deadline_ticks;
    struct RibonUpdateTransactionObserver *observer;
    uint64_t reserved[4];
};

/** @brief Authorization과 current slot identity에서 재개 가능한 install plan을 만든다. */
struct RibonUpdateInstallPlan {
    uint32_t size;
    uint32_t abi_version;
    enum RibonUpdateSlotState resume_state;
    uint32_t target_slot;
    uint32_t component_count;
    uint32_t flags;
    uint8_t manifest_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t image_set_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    struct RibonUpdateSlotMetadata staging_metadata;
    uint64_t reserved[4];
};

/** @brief Successful install이 만든 independently reopenable metadata와 receipt다. */
struct RibonUpdateInstallResult {
    uint32_t size;
    uint32_t abi_version;
    uint32_t target_slot;
    uint32_t component_count;
    uint64_t installed_exact_bytes;
    uint64_t installed_backing_bytes;
    uint8_t manifest_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t image_set_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    struct RibonUpdateSlotMetadata verified_metadata;
};

/** @brief Signed bundle install의 stable fail-closed status다. */
enum RibonUpdateInstallStatus {
    RIBON_UPDATE_INSTALL_STATUS_OK = 0,
    RIBON_UPDATE_INSTALL_STATUS_INVALID_ARGUMENT = -1,
    RIBON_UPDATE_INSTALL_STATUS_AUTHORIZATION = -2,
    RIBON_UPDATE_INSTALL_STATUS_LAYOUT = -3,
    RIBON_UPDATE_INSTALL_STATUS_CAPACITY = -4,
    RIBON_UPDATE_INSTALL_STATUS_BUNDLE_IO = -5,
    RIBON_UPDATE_INSTALL_STATUS_COMPONENT_DIGEST = -6,
    RIBON_UPDATE_INSTALL_STATUS_STORAGE_IO = -7,
    RIBON_UPDATE_INSTALL_STATUS_READBACK_DIGEST = -8,
    RIBON_UPDATE_INSTALL_STATUS_STATE = -9,
    RIBON_UPDATE_INSTALL_STATUS_INTERRUPTED = -10,
};

/** @brief Signed manifest를 승인하고 EMPTY 또는 동일 identity retry plan을 만든다. */
int ribon_update_install_prepare(
    const struct RibonUpdateInstallRequest *request,
    struct RibonUpdateInstallPlan *plan);

/** @brief Signed bundle을 inactive slot에 설치하고 full readback 뒤 VERIFIED로 닫는다. */
int ribon_update_install_signed_bundle(
    const struct RibonUpdateInstallRequest *request,
    struct RibonUpdateInstallResult *result);

/** @brief Installer status의 stable diagnostic name을 반환한다. */
const char *ribon_update_install_status_name(enum RibonUpdateInstallStatus status);

#endif
