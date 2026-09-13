#include <Ribon/boot/plan.h>
#include <Ribon/plugin/descriptor.h>
#include <Ribon/protocols/os/luca/direct_fdt.h>

#include <stdio.h>
#include <string.h>

#define TEST_KERNEL_BASE 0x40080000ull
#define TEST_KERNEL_SIZE 0x00200000ull
#define TEST_WORLD_BASE 0x52000000ull
#define TEST_WORLD_SIZE 0x00100000ull

static void put_be32(unsigned char *bytes, uint32_t value) {
    bytes[0] = (unsigned char)(value >> 24u);
    bytes[1] = (unsigned char)(value >> 16u);
    bytes[2] = (unsigned char)(value >> 8u);
    bytes[3] = (unsigned char)value;
}

static uint64_t get_be64(const unsigned char *bytes) {
    uint64_t value = 0u;
    for (uint32_t index = 0u; index < 8u; ++index) {
        value = (value << 8u) | bytes[index];
    }
    return value;
}

/** @brief root #address-cells=2와 empty /chosen을 가진 최소 FDT를 만든다. */
static uint32_t make_fdt(unsigned char *bytes) {
    const uint32_t structure_offset = 56u;
    const uint32_t strings_offset = 104u;
    const uint32_t total_size = 119u;
    memset(bytes, 0, total_size);
    put_be32(bytes, 0xd00dfeedu);
    put_be32(bytes + 4u, total_size);
    put_be32(bytes + 8u, structure_offset);
    put_be32(bytes + 12u, strings_offset);
    put_be32(bytes + 16u, 40u);
    put_be32(bytes + 20u, 17u);
    put_be32(bytes + 24u, 16u);
    put_be32(bytes + 32u, 15u);
    put_be32(bytes + 36u, 48u);
    put_be32(bytes + 56u, 1u);
    put_be32(bytes + 64u, 3u);
    put_be32(bytes + 68u, 4u);
    put_be32(bytes + 72u, 0u);
    put_be32(bytes + 76u, 2u);
    put_be32(bytes + 80u, 1u);
    memcpy(bytes + 84u, "chosen", 7u);
    put_be32(bytes + 92u, 2u);
    put_be32(bytes + 96u, 2u);
    put_be32(bytes + 100u, 9u);
    memcpy(bytes + strings_offset, "#address-cells", 15u);
    return total_size;
}

static int contains(const unsigned char *bytes, uint64_t size, const char *text) {
    const size_t length = strlen(text);
    if (length == 0u || size < length) {
        return 0;
    }
    for (uint64_t index = 0u; index <= size - length; ++index) {
        if (memcmp(bytes + index, text, length) == 0) {
            return 1;
        }
    }
    return 0;
}

static int reserve_map_contains(
    const unsigned char *fdt,
    uint64_t fdt_size,
    uint64_t base,
    uint64_t size) {
    uint32_t cursor;
    if (fdt_size < 40u) {
        return 0;
    }
    cursor = ((uint32_t)fdt[16] << 24u) | ((uint32_t)fdt[17] << 16u) |
             ((uint32_t)fdt[18] << 8u) | fdt[19];
    while (cursor <= fdt_size && fdt_size - cursor >= 16u) {
        const uint64_t observed_base = get_be64(fdt + cursor);
        const uint64_t observed_size = get_be64(fdt + cursor + 8u);
        if (observed_base == 0u && observed_size == 0u) {
            return 0;
        }
        if (base >= observed_base && size <= observed_size &&
            base - observed_base <= observed_size - size) {
            return 1;
        }
        cursor += 16u;
    }
    return 0;
}

int main(void) {
    const struct RibonBootProtocol *protocol =
        ribon_luca_direct_fdt_protocol_plugin_descriptor.operations;
    const struct RibonArchDescriptor arch = {
        .id = RIBON_ARCHITECTURE_AARCH64,
    };
    struct RibonLoadSegment segments[2] = {
        {
            .load_address = TEST_KERNEL_BASE,
            .runtime_address = TEST_KERNEL_BASE,
            .memory_size = TEST_KERNEL_SIZE,
            .flags = RIBON_LOAD_SEGMENT_EXECUTE,
        },
        {
            .load_address = TEST_KERNEL_BASE + TEST_KERNEL_SIZE,
            .runtime_address = TEST_KERNEL_BASE + TEST_KERNEL_SIZE,
            .memory_size = 0x00400000u,
            .flags = RIBON_LOAD_SEGMENT_WRITE,
        },
    };
    struct RibonBootPlan plan = {
        .environment = RIBON_ENVIRONMENT_UEFI,
        .arch = &arch,
        .kernel_runtime_entry_address = TEST_KERNEL_BASE,
        .kernel_load_segments = segments,
        .kernel_load_segment_count = 2u,
    };
    unsigned char source_fdt[256];
    const uint32_t source_fdt_size = make_fdt(source_fdt);
    const struct RibonBootModule world = {
        .name = "/RIBON/WORLD.PKG",
        .physical_address = TEST_WORLD_BASE,
        .size = TEST_WORLD_SIZE,
        .role = RIBON_BOOT_MODULE_ROLE_INITIAL_IMAGE,
    };
    struct RibonBootEnvironment environment = {
        .size = sizeof(environment),
        .abi_version = RIBON_CORE_ABI_VERSION,
        .kind = RIBON_ENVIRONMENT_UEFI,
        .architecture = RIBON_ARCHITECTURE_AARCH64,
        .device_tree = {
            .physical_address = (uint64_t)(uintptr_t)source_fdt,
            .size = source_fdt_size,
            .data = source_fdt,
        },
        .boot_modules = {.modules = &world, .module_count = 1u},
        .command_line = {.text = "", .length = 0u},
        .flags = RIBON_BOOT_ENV_HAS_MEMORY_MAP |
                 RIBON_BOOT_ENV_HAS_DEVICE_TREE |
                 RIBON_BOOT_ENV_HAS_BOOT_MODULES,
    };
    struct RibonMemoryRegion final_regions[2] = {
        {
            .base = 0x40000000u,
            .length = 0x20000000u,
            .kind = RIBON_MEMORY_REGION_USABLE,
            .attributes = RIBON_MEMORY_ATTR_READ | RIBON_MEMORY_ATTR_WRITE,
        },
        {
            .base = 0x50000000u,
            .length = 0x10000u,
            .kind = RIBON_MEMORY_REGION_FIRMWARE,
            .attributes = RIBON_MEMORY_ATTR_READ | RIBON_MEMORY_ATTR_FIRMWARE_RUNTIME,
        },
    };
    struct RibonMutableMemoryMap memory_map = {
        .regions = final_regions,
        .region_count = 2u,
        .capacity = 2u,
    };
    unsigned char output[4096];
    unsigned char second_output[4096];
    struct RibonHandoffArtifact handoff;
    struct RibonTerminalRequest terminal;
    struct RibonManifestView manifest = {
        .protocol_id = "luca-direct-fdt-dev",
        .protocol_abi_min = 1u,
        .protocol_abi_max = 1u,
    };

    if (!ribon_protocol_plugin_operations_are_valid(
            &ribon_luca_direct_fdt_protocol_plugin_descriptor) ||
        strcmp(ribon_luca_direct_fdt_protocol_plugin_descriptor.id,
               "protocol.luca-direct-fdt-dev") != 0 ||
        protocol->ops->match(&manifest) != RIBON_PROTOCOL_STATUS_OK ||
        protocol->ops->prepare_handoff(
            &plan, &environment, &memory_map,
            output, sizeof(output), &handoff) != RIBON_PROTOCOL_HANDOFF_STATUS_OK ||
        strcmp(handoff.format, "fdt") != 0 ||
        !contains(output, handoff.size, "linux,initrd-start") ||
        !contains(output, handoff.size, "linux,initrd-end") ||
        !contains(output, handoff.size, "luca,kernel-physical-start") ||
        !contains(output, handoff.size, "luca,kernel-physical-end") ||
        !contains(output, handoff.size, "luca,kernel-entry") ||
        !reserve_map_contains(output, handoff.size,
                              TEST_KERNEL_BASE, TEST_KERNEL_SIZE) ||
        !reserve_map_contains(output, handoff.size,
                              TEST_WORLD_BASE, TEST_WORLD_SIZE) ||
        !reserve_map_contains(output, handoff.size, 0x50000000u, 0x10000u) ||
        !ribon_luca_direct_fdt_runtime_span_valid(
            TEST_KERNEL_BASE, TEST_KERNEL_SIZE) ||
        !ribon_luca_direct_fdt_runtime_span_valid(
            TEST_WORLD_BASE, TEST_WORLD_SIZE)) {
        fputs("luca_direct_fdt_tests: positive handoff failed\n", stderr);
        return 1;
    }
    handoff.data = (void *)(uintptr_t)0x60000000u;
    if (
        protocol->ops->prepare_terminal(
            &arch, &plan, &environment, &handoff, &terminal) !=
                RIBON_PROTOCOL_STATUS_OK ||
        terminal.direct_entry.argument_count != 1u ||
        terminal.direct_entry.arguments[0] != UINT64_C(0x60000000) ||
        terminal.direct_entry.arguments[1] != 0u ||
        terminal.direct_entry.translation != RIBON_ENTRY_TRANSLATION_DISABLED ||
        terminal.direct_entry.privilege != RIBON_ENTRY_PRIVILEGE_AARCH64_EL1) {
        fputs("luca_direct_fdt_tests: positive contract failed\n", stderr);
        return 1;
    }

    if (ribon_luca_direct_fdt_runtime_span_valid(
            RIBON_LUCA_DIRECT_FDT_RUNTIME_RAM_START - 4096u, 4096u) ||
        ribon_luca_direct_fdt_runtime_span_valid(
            RIBON_LUCA_DIRECT_FDT_RUNTIME_RAM_END - 4096u, 8192u)) {
        fputs("luca_direct_fdt_tests: runtime window escape accepted\n", stderr);
        return 1;
    }

    manifest.protocol_id = "luca";
    if (protocol->ops->match(&manifest) == RIBON_PROTOCOL_STATUS_OK) {
        fputs("luca_direct_fdt_tests: RLH1 identity accepted\n", stderr);
        return 1;
    }
    manifest.protocol_id = "luca-direct-fdt-dev";
    environment.boot_modules.module_count = 0u;
    if (protocol->ops->prepare_handoff(
            &plan, &environment, &memory_map,
            second_output, sizeof(second_output), &handoff) ==
        RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        fputs("luca_direct_fdt_tests: missing World accepted\n", stderr);
        return 1;
    }
    environment.boot_modules.module_count = 1u;
    segments[0].runtime_address = TEST_WORLD_BASE;
    if (protocol->ops->prepare_handoff(
            &plan, &environment, &memory_map,
            second_output, sizeof(second_output), &handoff) ==
        RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        fputs("luca_direct_fdt_tests: kernel/World overlap accepted\n", stderr);
        return 1;
    }
    segments[0].runtime_address = TEST_KERNEL_BASE;
    source_fdt[0] = 0u;
    if (protocol->ops->prepare_handoff(
            &plan, &environment, &memory_map,
            second_output, sizeof(second_output), &handoff) ==
        RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        fputs("luca_direct_fdt_tests: malformed FDT accepted\n", stderr);
        return 1;
    }
    source_fdt[0] = 0xd0u;
    if (protocol->ops->prepare_handoff(
            &plan, &environment, &memory_map,
            second_output, 64u, &handoff) ==
        RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        fputs("luca_direct_fdt_tests: undersized output accepted\n", stderr);
        return 1;
    }
    environment.device_tree.data = output;
    environment.device_tree.physical_address = (uint64_t)(uintptr_t)output;
    environment.device_tree.size = handoff.size;
    if (protocol->ops->prepare_handoff(
            &plan, &environment, &memory_map,
            second_output, sizeof(second_output), &handoff) ==
        RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        fputs("luca_direct_fdt_tests: duplicate World properties accepted\n", stderr);
        return 1;
    }
    environment.device_tree.data = source_fdt;
    environment.device_tree.size = source_fdt_size;
    {
        const struct RibonArchDescriptor wrong_arch = {
            .id = RIBON_ARCHITECTURE_X86_64,
        };
        if (protocol->ops->prepare_terminal(
                &wrong_arch, &plan, &environment, &handoff, &terminal) ==
            RIBON_PROTOCOL_STATUS_OK) {
            fputs("luca_direct_fdt_tests: wrong entry architecture accepted\n", stderr);
            return 1;
        }
    }
    puts("RIBON-LUCA-DIRECT-FDT-CONTRACT-OK");
    return 0;
}
