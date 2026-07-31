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
    uint32_t initial_images = 0u;
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
    if (((environment->flags & RIBON_BOOT_ENV_HAS_BOOT_MEDIA) != 0u) !=
        (environment->boot_media.kind != RIBON_BOOT_MEDIA_NONE)) {
        return 0;
    }
    if (((environment->flags & RIBON_BOOT_ENV_HAS_BOOT_MODULES) != 0u) !=
            (environment->boot_modules.module_count != 0u) ||
        (environment->boot_modules.module_count != 0u &&
         (environment->boot_modules.modules == 0 ||
          environment->boot_modules.module_count > RIBON_BOOT_MODULE_CAPACITY))) {
        return 0;
    }
    if (((environment->flags & RIBON_BOOT_ENV_HAS_COMMAND_LINE) != 0u) !=
        (environment->command_line.text != 0 &&
         environment->command_line.length != 0u)) {
        return 0;
    }
    for (uint32_t index = 0u;
         index < environment->boot_modules.module_count;
         ++index) {
        const struct RibonBootModule *module =
            &environment->boot_modules.modules[index];
        if (module->physical_address == 0u || module->size == 0u ||
            module->physical_address > UINT64_MAX - module->size ||
            (module->role != RIBON_BOOT_MODULE_ROLE_INITIAL_IMAGE &&
             module->role != RIBON_BOOT_MODULE_ROLE_AUXILIARY)) {
            return 0;
        }
        if (module->role == RIBON_BOOT_MODULE_ROLE_INITIAL_IMAGE &&
            ++initial_images > 1u) {
            return 0;
        }
        for (uint32_t previous = 0u; previous < index; ++previous) {
            const struct RibonBootModule *other =
                &environment->boot_modules.modules[previous];
            if (module->physical_address <
                    other->physical_address + other->size &&
                other->physical_address <
                    module->physical_address + module->size) {
                return 0;
            }
        }
    }
    return 1;
}

/** @brief Capture 결과에 target-owned persistent semantic input을 동일하게 재적용한다. */
int ribon_boot_environment_apply_persistent_inputs(
    struct RibonBootEnvironment *environment,
    const struct RibonBootEnvironmentPersistentInputs *inputs) {
    uint32_t persistent_flags = 0u;
    if (environment == 0 || inputs == 0) {
        return 0;
    }
    if (inputs->boot_media.kind != RIBON_BOOT_MEDIA_NONE) {
        persistent_flags |= RIBON_BOOT_ENV_HAS_BOOT_MEDIA;
    }
    if (inputs->boot_modules.module_count != 0u) {
        persistent_flags |= RIBON_BOOT_ENV_HAS_BOOT_MODULES;
    }
    if (inputs->command_line.text != 0 && inputs->command_line.length != 0u) {
        persistent_flags |= RIBON_BOOT_ENV_HAS_COMMAND_LINE;
    }
    environment->boot_media = inputs->boot_media;
    environment->boot_modules = inputs->boot_modules;
    environment->command_line = inputs->command_line;
    environment->flags &=
        ~(RIBON_BOOT_ENV_HAS_BOOT_MEDIA |
          RIBON_BOOT_ENV_HAS_BOOT_MODULES |
          RIBON_BOOT_ENV_HAS_COMMAND_LINE);
    environment->flags |= persistent_flags;
    return ribon_boot_environment_is_valid(environment);
}
