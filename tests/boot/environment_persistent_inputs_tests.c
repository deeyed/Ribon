#include <Ribon/firmware/environment.h>

#include <stdio.h>

/** @brief Final-map 재캡처가 typed target input을 잃지 않는지 검증한다. */
int main(void) {
    static const struct RibonMemoryRegion regions[] = {
        {
            .base = 0x100000u,
            .length = 0x100000u,
            .kind = RIBON_MEMORY_REGION_USABLE,
            .attributes = RIBON_MEMORY_ATTR_READ | RIBON_MEMORY_ATTR_WRITE,
        },
    };
    static const struct RibonBootModule modules[] = {
        {
            .name = "/RIBON/INIT.IMG",
            .physical_address = 0x300000u,
            .size = 0x1000u,
            .role = RIBON_BOOT_MODULE_ROLE_INITIAL_IMAGE,
        },
        {
            .name = "/RIBON/AUX.IMG",
            .physical_address = 0x310000u,
            .size = 0x1000u,
            .role = RIBON_BOOT_MODULE_ROLE_AUXILIARY,
        },
    };
    static const struct RibonBootEnvironmentPersistentInputs inputs = {
        .boot_media = {
            .kind = RIBON_BOOT_MEDIA_FILE,
            .path = "/RIBON/PAYLOAD.ELF",
            .size = 0x2000u,
        },
        .boot_modules = {
            .modules = modules,
            .module_count = 2u,
        },
        .command_line = {
            .text = "console=serial",
            .length = 14u,
        },
    };
    struct RibonBootEnvironment environment;
    for (uint32_t recapture = 0u; recapture < 3u; ++recapture) {
        ribon_boot_environment_init(
            &environment,
            RIBON_ENVIRONMENT_UEFI,
            RIBON_ARCHITECTURE_X86_64);
        environment.memory_map = (struct RibonMemoryMap){
            .regions = regions,
            .region_count = 1u,
        };
        environment.flags = RIBON_BOOT_ENV_HAS_MEMORY_MAP;
        if (!ribon_boot_environment_apply_persistent_inputs(
                &environment,
                &inputs) ||
            environment.boot_modules.modules != modules ||
            environment.boot_modules.module_count != 2u ||
            environment.boot_modules.modules[0].role !=
                RIBON_BOOT_MODULE_ROLE_INITIAL_IMAGE ||
            environment.command_line.text != inputs.command_line.text ||
            environment.boot_media.path != inputs.boot_media.path) {
            fputs(
                "environment_persistent_inputs_tests: recapture lost semantic input\n",
                stderr);
            return 1;
        }
    }
    puts("RIBON-UEFI-PERSISTENT-INPUTS-OK");
    return 0;
}
