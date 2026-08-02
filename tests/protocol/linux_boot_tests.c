#include <Ribon/boot/plan.h>
#include <Ribon/protocols/os/linux/boot.h>

#include <stdio.h>
#include <string.h>

#define TEST_KERNEL_BASE 0x41000000ull
#define TEST_KERNEL_SIZE 0x01020000ull
#define TEST_INITRD_BASE 0x40200000ull
#define TEST_INITRD_SIZE 0x00010000ull

static void put_be32(unsigned char *bytes, uint32_t value) {
    bytes[0] = (unsigned char)(value >> 24u);
    bytes[1] = (unsigned char)(value >> 16u);
    bytes[2] = (unsigned char)(value >> 8u);
    bytes[3] = (unsigned char)value;
}

static void put_le64(unsigned char *bytes, uint64_t value) {
    for (uint32_t index = 0u; index < 8u; ++index) {
        bytes[index] = (unsigned char)(value >> (index * 8u));
    }
}

/** @brief root #address-cells=2와 /chosen을 가진 최소 valid FDT를 만든다. */
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

int main(void) {
    const struct RibonBootProtocol *protocol =
        ribon_linux_protocol_plugin_descriptor.operations;
    const struct RibonImageFormatOps *image =
        ribon_linux_aarch64_image_plugin_descriptor.operations;
    const struct RibonArchDescriptor arch = {
        .id = RIBON_ARCHITECTURE_AARCH64,
    };
    struct RibonBootPlan plan = {
        .arch = &arch,
        .kernel_image = {
            .format = RIBON_EXECUTABLE_FORMAT_LINUX_AARCH64,
        },
        .kernel_runtime_entry_address = TEST_KERNEL_BASE,
        .kernel_runtime_load_base = TEST_KERNEL_BASE,
        .kernel_runtime_load_end = TEST_KERNEL_BASE + TEST_KERNEL_SIZE,
    };
    unsigned char fdt[256];
    const uint32_t fdt_size = make_fdt(fdt);
    const struct RibonBootModule module = {
        .name = "initramfs",
        .physical_address = TEST_INITRD_BASE,
        .size = TEST_INITRD_SIZE,
        .role = RIBON_BOOT_MODULE_ROLE_AUXILIARY,
    };
    struct RibonBootEnvironment environment = {
        .device_tree = {
            .physical_address = (uint64_t)(uintptr_t)fdt,
            .size = fdt_size,
            .data = fdt,
        },
        .boot_modules = {.modules = &module, .module_count = 1u},
        .flags = RIBON_BOOT_ENV_HAS_DEVICE_TREE | RIBON_BOOT_ENV_HAS_BOOT_MODULES,
    };
    struct RibonMemoryRegion reservation = {
        .base = TEST_INITRD_BASE,
        .length = TEST_INITRD_SIZE,
        .kind = RIBON_MEMORY_REGION_BOOT_MODULE,
    };
    struct RibonMutableMemoryMap memory_map = {
        .regions = &reservation,
        .region_count = 1u,
        .capacity = 1u,
    };
    struct RibonHandoffArtifact handoff;
    unsigned char output[512];
    struct RibonTerminalRequest terminal;
    struct RibonValidatedImage validated;
    unsigned char image_bytes[128] = {0};
    struct RibonLoadSegment segment;
    struct RibonDirectLoadPlan loaded = {
        .segments = &segment,
        .segment_capacity = 1u,
    };
    struct RibonPayloadImage payload = {
        .data = image_bytes,
        .size = sizeof(image_bytes),
        .source_name = "Image",
    };

    image_bytes[56] = 'A'; image_bytes[57] = 'R';
    image_bytes[58] = 'M'; image_bytes[59] = 'd';
    put_le64(image_bytes + 16u, 4096u);
    if (!ribon_protocol_plugin_operations_are_valid(
            &ribon_linux_protocol_plugin_descriptor) ||
        !ribon_image_plugin_operations_are_valid(
            &ribon_linux_aarch64_image_plugin_descriptor) ||
        image->analyze(&payload, &validated, &loaded) != RIBON_LOADER_STATUS_OK ||
        validated.format != RIBON_EXECUTABLE_FORMAT_LINUX_AARCH64 ||
        (loaded.load_plan_flags & RIBON_LOAD_PLAN_RELOCATABLE) == 0u ||
        protocol->ops->prepare_handoff(
            &plan, &environment, &memory_map,
            output, sizeof(output), &handoff) != RIBON_PROTOCOL_HANDOFF_STATUS_OK ||
        !contains(output, handoff.size, "linux,initrd-start") ||
        !contains(output, handoff.size, "linux,initrd-end") ||
        protocol->ops->prepare_terminal(
            &arch, &plan, &environment, &handoff, &terminal) !=
            RIBON_PROTOCOL_STATUS_OK ||
        terminal.direct_entry.arguments[0] != (uint64_t)(uintptr_t)output ||
        terminal.direct_entry.argument_count != 1u) {
        fputs("linux_boot_tests: positive contract failed\n", stderr);
        return 1;
    }

    environment.flags = RIBON_BOOT_ENV_HAS_BOOT_MODULES;
    if (protocol->ops->prepare_handoff(
            &plan, &environment, &memory_map,
            output, sizeof(output), &handoff) !=
            RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN) {
        fputs("linux_boot_tests: missing FDT accepted\n", stderr);
        return 1;
    }
    environment.flags =
        RIBON_BOOT_ENV_HAS_DEVICE_TREE | RIBON_BOOT_ENV_HAS_BOOT_MODULES;
    reservation.kind = RIBON_MEMORY_REGION_RESERVED;
    if (protocol->ops->prepare_handoff(
            &plan, &environment, &memory_map,
            output, sizeof(output), &handoff) !=
            RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN) {
        fputs("linux_boot_tests: unreserved initramfs accepted\n", stderr);
        return 1;
    }
    reservation.kind = RIBON_MEMORY_REGION_BOOT_MODULE;
    plan.kernel_runtime_load_base = TEST_INITRD_BASE;
    plan.kernel_runtime_load_end = TEST_INITRD_BASE + TEST_KERNEL_SIZE;
    if (protocol->ops->prepare_handoff(
            &plan, &environment, &memory_map,
            output, sizeof(output), &handoff) !=
            RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN) {
        fputs("linux_boot_tests: kernel/module overlap accepted\n", stderr);
        return 1;
    }
    plan.kernel_runtime_load_base = TEST_KERNEL_BASE;
    plan.kernel_runtime_load_end = TEST_KERNEL_BASE + TEST_KERNEL_SIZE;
    if (protocol->ops->prepare_handoff(
            &plan, &environment, &memory_map,
            output, 64u, &handoff) !=
            RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN) {
        fputs("linux_boot_tests: undersized handoff accepted\n", stderr);
        return 1;
    }
    image_bytes[56] = 0u;
    if (image->analyze(&payload, &validated, &loaded) == RIBON_LOADER_STATUS_OK) {
        fputs("linux_boot_tests: wrong image class accepted\n", stderr);
        return 1;
    }
    puts("RIBON-D07-LINUX-BOOT-CONTRACT-OK");
    return 0;
}
