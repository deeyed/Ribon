#include <Ribon/firmware/environment.h>

/** @brief Environment kind의 안정적인 이름을 반환한다. */
const char *ribon_environment_name(enum RibonEnvironmentKind environment) {
    switch (environment) {
    case RIBON_ENVIRONMENT_HOST:
        return "host";
    case RIBON_ENVIRONMENT_UEFI:
        return "uefi";
    case RIBON_ENVIRONMENT_BIOS:
        return "bios";
    case RIBON_ENVIRONMENT_RAW_FDT:
        return "raw-fdt";
    case RIBON_ENVIRONMENT_SBI:
        return "sbi";
    default:
        return "unknown";
    }
}

/** @brief Boot media kind의 안정적인 이름을 반환한다. */
const char *ribon_boot_media_name(enum RibonBootMediaKind media) {
    switch (media) {
    case RIBON_BOOT_MEDIA_NONE:
        return "none";
    case RIBON_BOOT_MEDIA_FILE:
        return "file";
    case RIBON_BOOT_MEDIA_BLOCK:
        return "block";
    case RIBON_BOOT_MEDIA_MEMORY:
        return "memory";
    case RIBON_BOOT_MEDIA_NETWORK:
        return "network";
    default:
        return "unknown";
    }
}

/** @brief Caller-owned environment를 비어 있는 valid ABI 상태로 초기화한다. */
void ribon_boot_environment_init(
    struct RibonBootEnvironment *environment,
    enum RibonEnvironmentKind kind,
    enum RibonArchitectureId architecture) {
    if (environment == 0) {
        return;
    }
    *environment = (struct RibonBootEnvironment){
        .size = sizeof(*environment),
        .abi_version = RIBON_CORE_ABI_VERSION,
        .kind = kind,
        .architecture = architecture,
    };
}

/** @brief Environment field와 flag/pointer 일관성을 검사한다. */
int ribon_boot_environment_is_valid(const struct RibonBootEnvironment *environment) {
    if (environment == 0 ||
        environment->size != sizeof(*environment) ||
        environment->abi_version != RIBON_CORE_ABI_VERSION ||
        environment->kind < RIBON_ENVIRONMENT_HOST ||
        environment->kind > RIBON_ENVIRONMENT_SBI ||
        environment->architecture < RIBON_ARCHITECTURE_X86_64 ||
        environment->architecture > RIBON_ARCHITECTURE_RISCV64) {
        return 0;
    }
    if ((environment->flags & RIBON_BOOT_ENV_HAS_MEMORY_MAP) != 0u &&
        (environment->memory_map.regions == 0 ||
         environment->memory_map.region_count == 0u)) {
        return 0;
    }
    if ((environment->flags & RIBON_BOOT_ENV_HAS_BOOT_MEDIA) != 0u &&
        environment->boot_media.kind == RIBON_BOOT_MEDIA_NONE) {
        return 0;
    }
    if ((environment->flags & RIBON_BOOT_ENV_HAS_BOOT_MODULES) != 0u &&
        (environment->boot_modules.modules == 0 ||
         environment->boot_modules.module_count == 0u)) {
        return 0;
    }
    return 1;
}
