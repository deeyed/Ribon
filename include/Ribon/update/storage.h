#ifndef RIBON_UPDATE_STORAGE_H
#define RIBON_UPDATE_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#include <Ribon/update/manifest.h>

/** @brief Generic update-storage native ABI version이다. */
#define RIBON_UPDATE_STORAGE_ABI_VERSION 1u

/** @brief Canonical A/B layout identity의 exact byte 길이다. */
#define RIBON_UPDATE_LAYOUT_IDENTITY_BYTES 512u

/** @brief Canonical slot metadata object의 exact byte 길이다. */
#define RIBON_UPDATE_SLOT_METADATA_BYTES 512u

/** @brief A/B layout이 갖는 image slot 수다. */
#define RIBON_UPDATE_SLOT_COUNT 2u

/** @brief Pending slot이 없음을 나타내는 canonical slot ID다. */
#define RIBON_UPDATE_SLOT_NONE UINT32_MAX

/** @brief Layout identity가 고정하는 canonical region 수다. */
#define RIBON_UPDATE_LAYOUT_REGION_COUNT 11u

/** @brief Product graph가 선택하는 update-storage adapter class다. */
enum RibonUpdateStorageProviderClass {
    RIBON_UPDATE_STORAGE_PROVIDER_CLASS_INVALID = 0,
    RIBON_UPDATE_STORAGE_PROVIDER_CLASS_FIRMWARE = 1,
    RIBON_UPDATE_STORAGE_PROVIDER_CLASS_NATIVE = 2,
    RIBON_UPDATE_STORAGE_PROVIDER_CLASS_REFERENCE = 3,
};

/** @brief Generated product graph가 Core와 adapter에 공유하는 immutable binding이다. */
struct RibonUpdateStorageProductBinding {
    uint32_t size; /**< `sizeof(struct RibonUpdateStorageProductBinding)`이다. */
    uint32_t abi_version; /**< `RIBON_UPDATE_STORAGE_ABI_VERSION`이다. */
    enum RibonUpdateStorageProviderClass provider_class; /**< Adapter authority class다. */
    uint32_t flags; /**< v1에서는 0이다. */
    const char *layout_id; /**< Diagnostic-only stable layout ID다. */
    uint8_t layout_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t media_identity_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    const char *read_service_id; /**< Typed boot-source service ID다. */
    const char *writer_service_id; /**< Typed inactive-slot writer service ID다. */
    const char *metadata_service_id; /**< Typed metadata service ID다. */
    const char *flush_service_id; /**< Typed durability service ID다. */
    uint64_t reserved[4]; /**< v1에서는 모두 0이다. */
};

/** @brief Update provider가 제공하는 exact operation capability다. */
enum RibonUpdateStorageCapability {
    RIBON_UPDATE_STORAGE_CAP_READ = 1u << 0,
    RIBON_UPDATE_STORAGE_CAP_WRITE = 1u << 1,
    RIBON_UPDATE_STORAGE_CAP_ERASE = 1u << 2,
    RIBON_UPDATE_STORAGE_CAP_FLUSH = 1u << 3,
};

/** @brief Recovery writer provider가 가져야 하는 complete capability set이다. */
#define RIBON_UPDATE_STORAGE_CAP_ALL \
    (RIBON_UPDATE_STORAGE_CAP_READ | RIBON_UPDATE_STORAGE_CAP_WRITE | \
     RIBON_UPDATE_STORAGE_CAP_ERASE | RIBON_UPDATE_STORAGE_CAP_FLUSH)

/** @brief Layout에서 semantic range를 식별하는 stable registry다. */
enum RibonUpdateLayoutRegionKind {
    RIBON_UPDATE_REGION_INVALID = 0,
    RIBON_UPDATE_REGION_BOOTLOADER = 1,
    RIBON_UPDATE_REGION_GUARD_BOOT_RECOVERY = 2,
    RIBON_UPDATE_REGION_IMMUTABLE_RECOVERY = 3,
    RIBON_UPDATE_REGION_GUARD_RECOVERY_SLOT_A = 4,
    RIBON_UPDATE_REGION_SLOT_A = 5,
    RIBON_UPDATE_REGION_GUARD_SLOT_A_SLOT_B = 6,
    RIBON_UPDATE_REGION_SLOT_B = 7,
    RIBON_UPDATE_REGION_GUARD_SLOT_B_METADATA = 8,
    RIBON_UPDATE_REGION_SLOT_METADATA = 9,
    RIBON_UPDATE_REGION_UPDATE_JOURNAL = 10,
    RIBON_UPDATE_REGION_TRAILING_RESERVED = 11,
};

/** @brief Slot metadata가 표현하는 stable lifecycle state다. */
enum RibonUpdateSlotState {
    RIBON_UPDATE_SLOT_EMPTY = 0,
    RIBON_UPDATE_SLOT_STAGING = 1,
    RIBON_UPDATE_SLOT_VERIFIED = 2,
    RIBON_UPDATE_SLOT_PENDING = 3,
    RIBON_UPDATE_SLOT_CONFIRMED = 4,
    RIBON_UPDATE_SLOT_BAD = 5,
};

/** @brief Layout, metadata와 provider operation의 stable fail-closed 결과다. */
enum RibonUpdateStorageStatus {
    RIBON_UPDATE_STORAGE_STATUS_OK = 0,
    RIBON_UPDATE_STORAGE_STATUS_INVALID_ARGUMENT = -1,
    RIBON_UPDATE_STORAGE_STATUS_UNSUPPORTED = -2,
    RIBON_UPDATE_STORAGE_STATUS_CAPACITY = -3,
    RIBON_UPDATE_STORAGE_STATUS_OVERFLOW = -4,
    RIBON_UPDATE_STORAGE_STATUS_ALIGNMENT = -5,
    RIBON_UPDATE_STORAGE_STATUS_OVERLAP = -6,
    RIBON_UPDATE_STORAGE_STATUS_MALFORMED = -7,
    RIBON_UPDATE_STORAGE_STATUS_SHORT_IO = -8,
    RIBON_UPDATE_STORAGE_STATUS_IO = -9,
    RIBON_UPDATE_STORAGE_STATUS_PROTECTED = -10,
    RIBON_UPDATE_STORAGE_STATUS_BAD_STATE = -11,
    RIBON_UPDATE_STORAGE_STATUS_IDENTITY_MISMATCH = -12,
};

/**
 * @brief Provider-private media에서 exact byte range를 읽는다.
 *
 * Callback은 `transferred`를 항상 설정해야 하며 Core는 requested size와 다른 성공을
 * short I/O로 거부한다.
 */
typedef int (*RibonUpdateStorageReadFn)(
    void *context,
    uint64_t offset,
    void *buffer,
    uint64_t size,
    uint64_t *transferred,
    uint64_t deadline_ticks);

/**
 * @brief Provider-private media의 preflight-approved range에 exact bytes를 쓴다.
 *
 * Callback은 active-slot 정책을 결정하지 않으며 semantic protection은 Core가 먼저
 * 검사한다.
 */
typedef int (*RibonUpdateStorageWriteFn)(
    void *context,
    uint64_t offset,
    const void *buffer,
    uint64_t size,
    uint64_t *transferred,
    uint64_t deadline_ticks);

/** @brief Provider-private media의 aligned byte range를 erase한다. */
typedef int (*RibonUpdateStorageEraseFn)(
    void *context,
    uint64_t offset,
    uint64_t size,
    uint64_t deadline_ticks);

/** @brief Provider가 소유하는 media durability barrier를 실행한다. */
typedef int (*RibonUpdateStorageFlushFn)(
    void *context,
    uint64_t deadline_ticks);

/**
 * @brief Recovery/provisioning product가 선택하는 bounded update media provider다.
 *
 * Native controller, UEFI handle, file path와 raw device identity는 `context` 뒤에만
 * 존재한다. Core는 fixed geometry와 exact callback만 소비한다.
 */
struct RibonUpdateStorageProvider {
    uint32_t size; /**< `sizeof(struct RibonUpdateStorageProvider)`다. */
    uint32_t abi_version; /**< `RIBON_UPDATE_STORAGE_ABI_VERSION`이다. */
    uint32_t flags; /**< v1에서는 0이다. */
    uint32_t capabilities; /**< `RIBON_UPDATE_STORAGE_CAP_ALL` exact set이다. */
    uint64_t capacity_bytes; /**< Addressable media byte 수다. */
    uint64_t read_alignment; /**< Read offset와 size alignment다. */
    uint64_t write_alignment; /**< Write offset와 size alignment다. */
    uint64_t erase_alignment; /**< Erase offset와 size alignment다. */
    uint64_t maximum_transfer_bytes; /**< 한 read/write callback byte 상한이다. */
    uint8_t media_identity_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    void *context; /**< Product-selected provider-private borrowed state다. */
    RibonUpdateStorageReadFn read; /**< Exact bounded read callback이다. */
    RibonUpdateStorageWriteFn write; /**< Exact bounded write callback이다. */
    RibonUpdateStorageEraseFn erase; /**< Aligned erase callback이다. */
    RibonUpdateStorageFlushFn flush; /**< Durability barrier callback이다. */
    uint64_t reserved[4]; /**< v1에서는 모두 0이다. */
};

/** @brief Deterministic A/B layout calculator의 source-neutral 입력이다. */
struct RibonUpdateLayoutInput {
    uint32_t size; /**< 이 native input의 byte 크기다. */
    uint32_t abi_version; /**< `RIBON_UPDATE_STORAGE_ABI_VERSION`이다. */
    uint32_t flags; /**< v1에서는 0이다. */
    uint32_t reserved0; /**< v1에서는 0이다. */
    uint64_t media_capacity_bytes; /**< Product media capacity다. */
    uint64_t allocation_alignment; /**< 모든 canonical region alignment다. */
    uint64_t guard_gap_bytes; /**< Protected region 사이 reserved gap 크기다. */
    uint64_t bootloader_bytes; /**< Bootloader maximum byte 수다. */
    uint64_t immutable_recovery_bytes; /**< Factory recovery maximum byte 수다. */
    uint64_t slot_payload_bytes; /**< Slot A와 B 각각의 byte capacity다. */
    uint64_t slot_metadata_bytes; /**< Redundant metadata를 포함한 region byte 수다. */
    uint64_t update_journal_bytes; /**< Transaction journal reserved byte 수다. */
    uint64_t minimum_trailing_reserved_bytes; /**< Tail reserve minimum이다. */
    uint64_t reserved[4]; /**< v1에서는 모두 0이다. */
};

/** @brief Canonical layout 안의 한 half-open semantic byte range다. */
struct RibonUpdateLayoutRegion {
    enum RibonUpdateLayoutRegionKind kind; /**< Stable region kind다. */
    uint32_t flags; /**< v1에서는 0이다. */
    uint64_t offset; /**< Media-relative byte offset이다. */
    uint64_t length; /**< Non-zero region byte 수다. */
};

/** @brief 검증된 deterministic A/B layout과 canonical digest다. */
struct RibonUpdateLayout {
    uint32_t size; /**< 이 native view의 byte 크기다. */
    uint32_t abi_version; /**< `RIBON_UPDATE_STORAGE_ABI_VERSION`이다. */
    uint32_t flags; /**< v1에서는 0이다. */
    uint32_t region_count; /**< `RIBON_UPDATE_LAYOUT_REGION_COUNT`다. */
    uint64_t media_capacity_bytes; /**< Exact product media capacity다. */
    uint64_t allocation_alignment; /**< Canonical region alignment다. */
    struct RibonUpdateLayoutRegion regions[RIBON_UPDATE_LAYOUT_REGION_COUNT];
    uint8_t identity_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint64_t reserved[4]; /**< v1에서는 모두 0이다. */
};

/** @brief 한 image slot의 독립 metadata와 content identity다. */
struct RibonUpdateSlotEntry {
    uint32_t slot_id; /**< A=0, B=1 stable ID다. */
    enum RibonUpdateSlotState state; /**< Slot lifecycle state다. */
    uint64_t metadata_generation; /**< 이 entry를 마지막으로 바꾼 metadata generation이다. */
    uint64_t image_generation; /**< Image set의 독립 generation이다. */
    uint8_t manifest_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t image_set_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t layout_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint32_t boot_attempts; /**< Pending attempt count, 최대 32다. */
    uint32_t flags; /**< v1에서는 0이다. */
    uint64_t reserved[4]; /**< v1에서는 모두 0이다. */
};

/** @brief Explicit LE slot metadata codec의 native input/view다. */
struct RibonUpdateSlotMetadata {
    uint32_t size; /**< 이 native object의 byte 크기다. */
    uint32_t abi_version; /**< `RIBON_UPDATE_STORAGE_ABI_VERSION`이다. */
    uint32_t flags; /**< v1에서는 0이다. */
    uint32_t active_slot; /**< Active confirmed slot ID다. */
    uint32_t pending_slot; /**< Pending slot 또는 `RIBON_UPDATE_SLOT_NONE`이다. */
    uint32_t slot_count; /**< `RIBON_UPDATE_SLOT_COUNT`다. */
    uint64_t metadata_generation; /**< Wrap하지 않는 positive generation이다. */
    struct RibonUpdateSlotEntry slots[RIBON_UPDATE_SLOT_COUNT];
    uint8_t wire_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint64_t reserved[4]; /**< v1에서는 모두 0이다. */
};

/** @brief Caller가 요청하는 단일 slot lifecycle transition이다. */
struct RibonUpdateSlotTransition {
    uint32_t size; /**< 이 request의 byte 크기다. */
    uint32_t abi_version; /**< `RIBON_UPDATE_STORAGE_ABI_VERSION`이다. */
    uint32_t flags; /**< v1에서는 0이다. */
    uint32_t slot_id; /**< 변경할 inactive slot ID다. */
    enum RibonUpdateSlotState next_state; /**< 허용 edge의 destination이다. */
    uint32_t boot_attempts; /**< Pending 진입 때 1..32, 그 외 0..32다. */
    uint64_t image_generation; /**< Slot image identity generation이다. */
    uint8_t manifest_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t image_set_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t layout_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint64_t reserved[4]; /**< v1에서는 모두 0이다. */
};

/**
 * @brief Slot byte offset을 policy에서 숨기는 caller-owned semantic handle이다.
 *
 * Handle은 생성한 session과 metadata generation에서만 유효하다. Ribos helper에는 이
 * native layout을 노출하지 않고 별도 VM opaque handle로 낮춘다.
 */
struct RibonUpdateSlotHandle {
    uint32_t size; /**< 이 handle의 byte 크기다. */
    uint32_t abi_version; /**< `RIBON_UPDATE_STORAGE_ABI_VERSION`이다. */
    uint32_t slot_id; /**< Core가 확인한 inactive slot이다. */
    uint32_t flags; /**< v1에서는 0이다. */
    uint64_t metadata_generation; /**< Open 시점 metadata generation이다. */
    uint8_t media_identity_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
    uint8_t layout_digest[RIBON_UPDATE_MANIFEST_DIGEST_BYTES];
};

/** @brief Provider, layout와 metadata를 결속한 allocation-free update session이다. */
struct RibonUpdateStorageSession {
    uint32_t size; /**< 이 session의 byte 크기다. */
    uint32_t abi_version; /**< `RIBON_UPDATE_STORAGE_ABI_VERSION`이다. */
    uint32_t flags; /**< v1에서는 0이다. */
    uint32_t reserved0; /**< v1에서는 0이다. */
    const struct RibonUpdateStorageProvider *provider; /**< Borrowed provider다. */
    const struct RibonUpdateLayout *layout; /**< Borrowed immutable layout이다. */
    const struct RibonUpdateSlotMetadata *metadata; /**< Borrowed metadata snapshot이다. */
    uint64_t reserved[4]; /**< v1에서는 모두 0이다. */
};

/** @brief Provider ABI, capability, geometry와 callbacks를 fail-closed 검사한다. */
int ribon_update_storage_provider_is_valid(
    const struct RibonUpdateStorageProvider *provider);

/** @brief Source-neutral input에서 deterministic A/B layout을 계산한다. */
int ribon_update_layout_calculate(
    const struct RibonUpdateLayoutInput *input,
    struct RibonUpdateLayout *layout);

/** @brief Layout을 canonical 512-byte little-endian identity로 직렬화한다. */
int ribon_update_layout_identity_encode(
    const struct RibonUpdateLayout *layout,
    uint8_t output[RIBON_UPDATE_LAYOUT_IDENTITY_BYTES]);

/** @brief Untrusted canonical layout identity를 독립 검증해 native view로 연다. */
int ribon_update_layout_identity_open(
    const uint8_t *bytes,
    size_t size,
    struct RibonUpdateLayout *layout);

/** @brief Generated update-storage binding의 ABI와 identities를 검사한다. */
int ribon_update_storage_product_binding_is_valid(
    const struct RibonUpdateStorageProductBinding *binding);

/** @brief Manifest component maximum range가 요구하는 aligned slot byte 수를 계산한다. */
int ribon_update_manifest_required_slot_bytes(
    const struct RibonUpdateManifestView *manifest,
    uint64_t alignment,
    uint64_t *required_bytes);

/** @brief Layout의 A/B slot이 manifest maximum component range를 수용하는지 검사한다. */
int ribon_update_layout_accepts_manifest(
    const struct RibonUpdateLayout *layout,
    const struct RibonUpdateManifestView *manifest);

/** @brief Native slot metadata를 canonical 512-byte LE wire로 직렬화한다. */
int ribon_update_slot_metadata_encode(
    const struct RibonUpdateSlotMetadata *metadata,
    uint8_t output[RIBON_UPDATE_SLOT_METADATA_BYTES]);

/** @brief Untrusted slot metadata wire를 독립 검증해 native snapshot으로 연다. */
int ribon_update_slot_metadata_open(
    const uint8_t *bytes,
    size_t size,
    struct RibonUpdateSlotMetadata *metadata);

/** @brief Current metadata에서 허용된 단일 slot transition을 생성한다. */
int ribon_update_slot_metadata_transition(
    const struct RibonUpdateSlotMetadata *current,
    const struct RibonUpdateSlotTransition *transition,
    struct RibonUpdateSlotMetadata *next);

/** @brief Session ABI와 provider/layout/metadata identity 결속을 검사한다. */
int ribon_update_storage_session_is_valid(
    const struct RibonUpdateStorageSession *session);

/** @brief STAGING 상태인 inactive slot을 semantic handle로 연다. */
int ribon_update_storage_open_inactive_slot(
    const struct RibonUpdateStorageSession *session,
    uint32_t slot_id,
    struct RibonUpdateSlotHandle *handle);

/** @brief Semantic slot handle 안의 exact bounded range를 읽는다. */
int ribon_update_storage_read(
    const struct RibonUpdateStorageSession *session,
    const struct RibonUpdateSlotHandle *handle,
    uint64_t slot_offset,
    void *buffer,
    uint64_t size,
    uint64_t deadline_ticks);

/** @brief Active confirmed slot을 제외한 STAGING handle에 aligned bytes를 쓴다. */
int ribon_update_storage_write_inactive(
    const struct RibonUpdateStorageSession *session,
    const struct RibonUpdateSlotHandle *handle,
    uint64_t slot_offset,
    const void *buffer,
    uint64_t size,
    uint64_t deadline_ticks);

/** @brief Active confirmed slot을 제외한 STAGING handle range를 erase한다. */
int ribon_update_storage_erase_inactive(
    const struct RibonUpdateStorageSession *session,
    const struct RibonUpdateSlotHandle *handle,
    uint64_t slot_offset,
    uint64_t size,
    uint64_t deadline_ticks);

/** @brief Selected provider의 explicit durability barrier를 실행한다. */
int ribon_update_storage_flush(
    const struct RibonUpdateStorageSession *session,
    uint64_t deadline_ticks);

/** @brief Update-storage status의 stable diagnostic name을 반환한다. */
const char *ribon_update_storage_status_name(
    enum RibonUpdateStorageStatus status);

#endif
