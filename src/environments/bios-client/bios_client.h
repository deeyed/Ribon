#ifndef RIBON_ENVIRONMENTS_BIOS_CLIENT_H
#define RIBON_ENVIRONMENTS_BIOS_CLIENT_H

#include <Ribon/firmware/environment.h>
#include <Ribon/service/directory.h>
#include <Ribon/plugin/descriptor.h>

/** @brief BIOS E820 native entry의 bounded semantic view다. */
struct RibonBiosE820Entry {
    uint64_t base; /**< Physical range 시작이다. */
    uint64_t length; /**< Physical range byte 수다. */
    uint32_t type; /**< BIOS E820 memory type이다. */
    uint32_t attributes; /**< Extended attributes다. */
};

/** @brief BIOS client가 real-mode trampoline을 통해 소비하는 native operation이다. */
struct RibonBiosNativeOps {
    uint32_t size; /**< Operation table byte 크기다. */
    int (*e820_next)(
        void *context,
        uint32_t *continuation,
        struct RibonBiosE820Entry *out); /**< 0은 item, 1은 end다. */
    int (*edd_read)(
        void *context,
        uint64_t lba,
        void *buffer,
        uint32_t sectors); /**< Bounded EDD sector read다. */
    uint64_t (*monotonic_counter)(void *context); /**< Preboot counter read다. */
};

/** @brief Long-mode bridge가 transfer 전에 검증하는 native precondition이다. */
struct RibonBiosLongModeContract {
    uint32_t size; /**< Contract byte 크기다. */
    uint32_t a20_enabled; /**< A20 gate가 열린 경우 1이다. */
    uint32_t long_mode_supported; /**< CPUID long-mode bit 검증 결과다. */
    uint32_t interrupts_masked; /**< PIC/NMI 전환 정책의 mask 결과다. */
    uint64_t page_table_base; /**< 4-KiB aligned transition table이다. */
    uint64_t entry_point; /**< 64-bit entry address다. */
};

/** @brief BIOS client environment의 caller-owned native context다. */
struct RibonBiosClientContext {
    const struct RibonBiosNativeOps *native; /**< Target-private trampoline table이다. */
    void *native_context; /**< Trampoline이 소유하는 opaque state다. */
    struct RibonMemoryRegion *regions; /**< Caller-owned E820 conversion storage다. */
    uint32_t region_capacity; /**< Region element 상한이다. */
    uint32_t region_count; /**< 마지막 capture element 수다. */
    uint32_t boot_drive; /**< BIOS DL boot-drive 값이다. */
    uint32_t block_size; /**< EDD logical sector byte 수다. */
    uint64_t media_sectors; /**< Boot media sector 수 상한이다. */
};

/** @brief BIOS client contract 결과다. */
enum RibonBiosClientStatus {
    RIBON_BIOS_CLIENT_STATUS_OK = 0,
    RIBON_BIOS_CLIENT_STATUS_BAD_ARGUMENT = -1,
    RIBON_BIOS_CLIENT_STATUS_NATIVE_ERROR = -2,
    RIBON_BIOS_CLIENT_STATUS_OUT_OF_CAPACITY = -3,
    RIBON_BIOS_CLIENT_STATUS_BAD_MEMORY_MAP = -4,
    RIBON_BIOS_CLIENT_STATUS_BAD_LONG_MODE = -5,
};

/** @brief E820과 EDD boundary를 firmware-neutral environment로 capture한다. */
int ribon_bios_client_capture(
    struct RibonBiosClientContext *context,
    struct RibonBootEnvironment *out);

/** @brief BIOS protected/long-mode transfer precondition을 fail-closed로 검사한다. */
int ribon_bios_long_mode_contract_is_valid(
    const struct RibonBiosLongModeContract *contract);

/** @brief 초기화된 BIOS client typed service directory를 반환한다. */
const struct RibonServiceDirectory *ribon_bios_client_service_directory(void);

/** @brief BIOS client environment consumer plugin descriptor다. */
extern const struct RibonPluginDescriptor ribon_bios_client_environment_plugin_descriptor;

#endif
