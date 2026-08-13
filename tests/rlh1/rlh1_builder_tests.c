#include <Ribon/arch/entry.h>
#include <Ribon/boot/plan.h>
#include <Ribon/core/memory.h>
#include <Ribon/protocols/os/luca/rlh1.h>

#include <stdio.h>
#include <string.h>

static int failures;

static const struct RibonArchDescriptor x86_64_arch = {
    .size = sizeof(x86_64_arch),
    .abi_version = RIBON_ARCH_OPS_ABI_VERSION,
    .id = RIBON_ARCHITECTURE_X86_64,
    .canonical_name = "x86_64",
    .elf_machine = 62u,
    .pe_coff_machine = 0x8664u,
};

static const struct RibonArchDescriptor riscv64_arch = {
    .size = sizeof(riscv64_arch),
    .abi_version = RIBON_ARCH_OPS_ABI_VERSION,
    .id = RIBON_ARCHITECTURE_RISCV64,
    .canonical_name = "riscv64",
    .elf_machine = 243u,
    .pe_coff_machine = 0u,
};

static uint16_t read_u16(const unsigned char *bytes, uint64_t offset) {
    return (uint16_t)bytes[offset] | ((uint16_t)bytes[offset + 1u] << 8u);
}

static uint32_t read_u32(const unsigned char *bytes, uint64_t offset) {
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1u] << 8u) |
           ((uint32_t)bytes[offset + 2u] << 16u) |
           ((uint32_t)bytes[offset + 3u] << 24u);
}

static uint64_t read_u64(const unsigned char *bytes, uint64_t offset) {
    uint64_t value = 0u;
    for (uint32_t index = 0; index < 8u; ++index) {
        value |= (uint64_t)bytes[offset + index] << (index * 8u);
    }
    return value;
}

static void write_u32(unsigned char *bytes, uint64_t offset, uint32_t value) {
    bytes[offset] = (unsigned char)(value & 0xffu);
    bytes[offset + 1u] = (unsigned char)((value >> 8u) & 0xffu);
    bytes[offset + 2u] = (unsigned char)((value >> 16u) & 0xffu);
    bytes[offset + 3u] = (unsigned char)((value >> 24u) & 0xffu);
}

static void write_u64(unsigned char *bytes, uint64_t offset, uint64_t value) {
    for (uint32_t index = 0; index < 8u; ++index) {
        bytes[offset + index] = (unsigned char)((value >> (index * 8u)) & 0xffu);
    }
}

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "RLH1-TEST-FAIL: %s\n", message);
        ++failures;
    }
}

static void refresh_checksum(unsigned char *bytes) {
    uint32_t total_size = read_u32(bytes, RIBON_LUCA_RLH1_HEADER_TOTAL_SIZE_OFFSET);
    write_u32(bytes, RIBON_LUCA_RLH1_HEADER_CRC32C_OFFSET, 0u);
    write_u32(
        bytes,
        RIBON_LUCA_RLH1_HEADER_CRC32C_OFFSET,
        ribon_luca_rlh1_crc32c(bytes, total_size));
}

static int build_fixture_with_arch_inputs(
    unsigned char *buffer,
    struct RibonHandoffArtifact *artifact,
    const struct RibonArchDescriptor *arch,
    enum RibonEnvironmentKind environment_kind,
    uint64_t boot_cpu_id,
    uint32_t environment_extra_flags,
    uint64_t device_tree_address,
    uint64_t device_tree_size,
    const char *command_line,
    uint64_t command_line_length,
    const struct RibonBootModule *modules,
    uint32_t module_count) {
    static const struct RibonLoadSegment segments[] = {
        {
            .file_offset = 0x100u,
            .file_size = 0x800u,
            .memory_size = 0x1000u,
            .virtual_address = 0xffffffff80000000ull,
            .linked_physical_address = 0x200000u,
            .physical_address = 0x200000u,
            .load_address = 0x200000u,
            .runtime_address = 0x200000u,
            .alignment = 0x1000u,
            .flags = RIBON_LOAD_SEGMENT_READ | RIBON_LOAD_SEGMENT_EXECUTE,
        },
    };
    static const struct RibonMemoryRegion regions[] = {
        {
            .base = 0x100000u,
            .length = 0x100000u,
            .kind = RIBON_MEMORY_REGION_USABLE,
            .attributes = RIBON_MEMORY_ATTR_READ | RIBON_MEMORY_ATTR_WRITE,
        },
        {
            .base = 0x200000u,
            .length = 0x1000u,
            .kind = RIBON_MEMORY_REGION_KERNEL_IMAGE,
            .attributes = RIBON_MEMORY_ATTR_READ | RIBON_MEMORY_ATTR_EXECUTE,
        },
    };
    struct RibonMutableMemoryMap map = {
        .regions = (struct RibonMemoryRegion *)regions,
        .region_count = 2u,
        .capacity = 2u,
    };
    struct RibonBootEnvironment environment = {
        .size = sizeof(environment),
        .abi_version = RIBON_CORE_ABI_VERSION,
        .kind = environment_kind,
        .architecture = arch->id,
        .boot_cpu_id = boot_cpu_id,
        .device_tree = {
            .physical_address = device_tree_address,
            .size = device_tree_size,
            .data = (const void *)(uintptr_t)device_tree_address,
        },
        .boot_media = {
            .kind = RIBON_BOOT_MEDIA_FILE,
            .size = 0x900u,
        },
        .command_line = {
            .text = command_line,
            .length = command_line_length,
        },
        .boot_modules = {
            .modules = modules,
            .module_count = module_count,
        },
        .flags = RIBON_BOOT_ENV_HAS_MEMORY_MAP |
                 RIBON_BOOT_ENV_HAS_BOOT_MEDIA |
                 RIBON_BOOT_ENV_HAS_COMMAND_LINE |
                 environment_extra_flags |
                 (module_count != 0u ? RIBON_BOOT_ENV_HAS_BOOT_MODULES : 0u),
    };
    struct RibonBootPlan plan = {
        .environment = environment_kind,
        .arch = arch,
        .kernel_load_segment_count = 1u,
        .kernel_entry_point = 0xffffffff80000080ull,
        .kernel_entry_load_address = 0x200080u,
        .kernel_runtime_entry_address = 0x200080u,
        .kernel_load_base = 0x200000u,
        .kernel_load_end = 0x201000u,
        .kernel_runtime_load_base = 0x200000u,
        .kernel_runtime_load_end = 0x201000u,
        .kernel_memory_size = 0x1000u,
        .kernel_linked_virtual_base = 0xffffffff80000000ull,
        .kernel_linked_virtual_end = 0xffffffff80001000ull,
        .kernel_linked_physical_base = 0x200000u,
        .kernel_linked_physical_end = 0x201000u,
        .kernel_high_entry_virtual_address = 0xffffffff80000000ull,
        .kernel_high_entry_load_address = 0x200000u,
        .kernel_load_segments = segments,
        .kernel_load_plan_flags = RIBON_LOAD_PLAN_ENTRY_LOAD_VALID |
                                  RIBON_LOAD_PLAN_RUNTIME_ENTRY_VALID |
                                  RIBON_LOAD_PLAN_USES_PADDR |
                                  RIBON_LOAD_PLAN_HAS_HIGHER_HALF,
    };
    memset(buffer, 0xa5, RIBON_LUCA_RLH1_MAX_TOTAL_SIZE);
    return ribon_luca_build_rlh1(
        &plan,
        &environment,
        &map,
        buffer,
        RIBON_LUCA_RLH1_MAX_TOTAL_SIZE,
        artifact);
}

static int build_fixture_with_inputs(
    unsigned char *buffer,
    struct RibonHandoffArtifact *artifact,
    const char *command_line,
    uint64_t command_line_length,
    const struct RibonBootModule *modules,
    uint32_t module_count) {
    return build_fixture_with_arch_inputs(
        buffer,
        artifact,
        &x86_64_arch,
        RIBON_ENVIRONMENT_UEFI,
        0u,
        0u,
        0u,
        0u,
        command_line,
        command_line_length,
        modules,
        module_count);
}

static int build_riscv_fixture(
    unsigned char *buffer,
    struct RibonHandoffArtifact *artifact,
    uint64_t boot_cpu_id,
    uint32_t environment_flags) {
    return build_fixture_with_arch_inputs(
        buffer,
        artifact,
        &riscv64_arch,
        RIBON_ENVIRONMENT_RAW_FDT,
        boot_cpu_id,
        environment_flags,
        0x88000000u,
        0x1000u,
        "protocol=luca",
        14u,
        0,
        0u);
}

static int build_fixture_with_command(
    unsigned char *buffer,
    struct RibonHandoffArtifact *artifact,
    const char *command_line,
    uint64_t command_line_length) {
    return build_fixture_with_inputs(
        buffer,
        artifact,
        command_line,
        command_line_length,
        0,
        0u);
}

static int build_fixture(
    unsigned char *buffer,
    struct RibonHandoffArtifact *artifact) {
    return build_fixture_with_command(
        buffer, artifact, "protocol=luca", 14u);
}

/** @brief Section type의 table entry와 payload를 bounded fixture 안에서 찾는다. */
static unsigned char *find_section(
    unsigned char *bytes,
    uint32_t type,
    unsigned char **payload_out) {
    const uint32_t table =
        read_u32(bytes, RIBON_LUCA_RLH1_HEADER_SECTION_TABLE_OFFSET);
    const uint16_t count =
        read_u16(bytes, RIBON_LUCA_RLH1_HEADER_SECTION_COUNT_OFFSET);
    for (uint16_t index = 0u; index < count; ++index) {
        unsigned char *entry =
            bytes + table +
            ((uint64_t)index * RIBON_LUCA_RLH1_SECTION_ENTRY_SIZE);
        if (read_u32(entry, RIBON_LUCA_RLH1_SECTION_TYPE_OFFSET) == type) {
            *payload_out =
                bytes + read_u64(
                    entry,
                    RIBON_LUCA_RLH1_SECTION_PAYLOAD_OFFSET);
            return entry;
        }
    }
    *payload_out = 0;
    return 0;
}

int main(void) {
    unsigned char valid[RIBON_LUCA_RLH1_MAX_TOTAL_SIZE];
    unsigned char mutated[RIBON_LUCA_RLH1_MAX_TOTAL_SIZE];
    char maximum_command_line[
        RIBON_LUCA_RLH1_COMMAND_LINE_MAX_SIZE + 1u];
    struct RibonHandoffArtifact artifact = {0};
    struct RibonLucaRlh1View view = {0};
    uint32_t total_size;
    uint32_t table_offset;
    uint64_t first_payload;
    static const struct RibonBootModule modules[] = {
        {
            .name = "init",
            .physical_address = 0x300000u,
            .size = 0x8000u,
            .role = RIBON_BOOT_MODULE_ROLE_INITIAL_IMAGE,
        },
        {
            .name = "aux",
            .physical_address = 0x310000u,
            .size = 0x4000u,
            .role = RIBON_BOOT_MODULE_ROLE_AUXILIARY,
        },
    };
    unsigned char *module_section;
    unsigned char *module_payload;
    unsigned char *boot_cpu_section;
    unsigned char *boot_cpu_payload;

    expect(RIBON_LUCA_ENTRY_FLAG_RLH1 == 0x1u, "RLH1 entry flag is bit 0");
    expect(RIBON_LUCA_ENTRY_FLAG_DIRECT_DTB == 0x2u, "direct DTB entry flag is bit 1");
    expect(RIBON_LUCA_ENTRY_FLAG_ENTERED_HIGH == 0x4u, "entered-high entry flag is bit 2");
    expect(RIBON_LUCA_ENTRY_FLAG_DIRECT_HIGH == 0x8u, "direct-high entry flag is bit 3");
    expect(build_fixture(valid, &artifact) == RIBON_PROTOCOL_HANDOFF_STATUS_OK, "builder accepts fixture");
    total_size = read_u32(valid, RIBON_LUCA_RLH1_HEADER_TOTAL_SIZE_OFFSET);
    table_offset = read_u32(valid, RIBON_LUCA_RLH1_HEADER_SECTION_TABLE_OFFSET);
    first_payload = read_u64(
        valid + table_offset,
        RIBON_LUCA_RLH1_SECTION_PAYLOAD_OFFSET);
    expect(
        first_payload ==
            RIBON_LUCA_RLH1_HEADER_SIZE +
                (10u * RIBON_LUCA_RLH1_SECTION_ENTRY_SIZE),
        "existing architecture payload table layout remains unchanged");

    expect(valid[0] == 'R' && valid[1] == 'L' && valid[2] == 'H' && valid[3] == '1',
           "wire magic is ASCII RLH1");
    expect(read_u16(valid, RIBON_LUCA_RLH1_HEADER_VERSION_MAJOR_OFFSET) == 1u,
           "major version is 1");
    expect(read_u16(valid, RIBON_LUCA_RLH1_HEADER_SIZE_OFFSET) == 80u,
           "header size is 80");
    expect(
        memcmp(
            valid + RIBON_LUCA_RLH1_HEADER_DOMAIN_OFFSET,
            "RIBON_LUCA_RLH1",
            RIBON_LUCA_RLH1_DOMAIN_SIZE) == 0,
        "header carries exact RLH1 domain separator");
    expect(read_u16(valid, RIBON_LUCA_RLH1_HEADER_SECTION_ENTRY_SIZE_OFFSET) == 32u,
           "section entry size is 32");
    expect(artifact.size == total_size && artifact.version_major == 1u,
           "artifact metadata reflects wire header");
    expect(strcmp(artifact.format, "rlh1") == 0, "artifact format is rlh1");
    expect(read_u32(valid, RIBON_LUCA_RLH1_HEADER_CRC32C_OFFSET) ==
               ribon_luca_rlh1_crc32c(valid, total_size),
           "stored CRC32C covers the complete artifact");
    expect(ribon_luca_parse_rlh1(valid, artifact.size, &view) ==
               RIBON_LUCA_RLH1_PARSE_OK,
           "parser accepts builder output");
    expect(view.total_size == artifact.size && view.section_count == artifact.section_count,
           "parser publishes bounded view metadata");
    expect(view.has_boot_cpu == 0u, "x86_64 artifact has no boot CPU section");

    memcpy(mutated, valid, artifact.size);
    write_u32(
        mutated,
        RIBON_LUCA_RLH1_HEADER_MAGIC_OFFSET,
        0x31485052u);
    expect(
        ribon_luca_parse_rlh1(mutated, artifact.size, &view) ==
            RIBON_LUCA_RLH1_PARSE_BAD_MAGIC,
        "parser rejects legacy RPH1 magic without downgrade");

    memcpy(mutated, valid, artifact.size);
    mutated[RIBON_LUCA_RLH1_HEADER_DOMAIN_OFFSET] ^= 0x1u;
    refresh_checksum(mutated);
    expect(
        ribon_luca_parse_rlh1(mutated, artifact.size, &view) ==
            RIBON_LUCA_RLH1_PARSE_BAD_HEADER,
        "parser rejects wrong RLH1 domain separator");

    memcpy(mutated, valid, artifact.size);
    mutated[RIBON_LUCA_RLH1_HEADER_SIZE_OFFSET] = 64u;
    mutated[RIBON_LUCA_RLH1_HEADER_SIZE_OFFSET + 1u] = 0u;
    refresh_checksum(mutated);
    expect(
        ribon_luca_parse_rlh1(mutated, artifact.size, &view) ==
            RIBON_LUCA_RLH1_PARSE_BAD_HEADER,
        "parser rejects mixed RLH1 magic with legacy header size");

    expect(
        build_riscv_fixture(
            valid,
            &artifact,
            7u,
            RIBON_BOOT_ENV_HAS_BOOT_CPU_ID |
                RIBON_BOOT_ENV_HAS_DEVICE_TREE) ==
            RIBON_PROTOCOL_HANDOFF_STATUS_OK,
        "builder accepts RISC-V bootstrap hart identity");
    expect(
        ribon_luca_parse_rlh1(valid, artifact.size, &view) ==
            RIBON_LUCA_RLH1_PARSE_OK &&
            view.has_boot_cpu == 1u &&
            view.boot_cpu_id == 7u &&
            view.boot_cpu_id_namespace ==
                RIBON_LUCA_RLH1_BOOT_CPU_NAMESPACE_RISCV_HART_ID &&
            view.boot_cpu_flags ==
                RIBON_LUCA_RLH1_BOOT_CPU_FLAG_BOOTSTRAP,
        "parser publishes RISC-V bootstrap hart identity");
    boot_cpu_section = find_section(
        valid,
        RIBON_LUCA_RLH1_SECTION_BOOT_CPU,
        &boot_cpu_payload);
    expect(
        boot_cpu_section != 0 && boot_cpu_payload != 0 &&
            read_u32(
                boot_cpu_section,
                RIBON_LUCA_RLH1_SECTION_FLAGS_OFFSET) ==
                RIBON_LUCA_RLH1_SECTION_REQUIRED_TO_UNDERSTAND &&
            read_u64(
                boot_cpu_section,
                RIBON_LUCA_RLH1_SECTION_LENGTH_OFFSET) ==
                RIBON_LUCA_RLH1_BOOT_CPU_SIZE &&
            read_u64(
                boot_cpu_payload,
                RIBON_LUCA_RLH1_BOOT_CPU_ID_OFFSET) == 7u,
        "RISC-V BOOT_CPU section has required fixed wire shape");

    memcpy(mutated, valid, artifact.size);
    boot_cpu_section = find_section(
        mutated,
        RIBON_LUCA_RLH1_SECTION_BOOT_CPU,
        &boot_cpu_payload);
    write_u64(
        boot_cpu_payload,
        RIBON_LUCA_RLH1_BOOT_CPU_RESERVED0_OFFSET,
        1u);
    refresh_checksum(mutated);
    expect(
        ribon_luca_parse_rlh1(mutated, artifact.size, &view) ==
            RIBON_LUCA_RLH1_PARSE_BAD_SECTION,
        "parser rejects nonzero BOOT_CPU reserved field");

    memcpy(mutated, valid, artifact.size);
    boot_cpu_section = find_section(
        mutated,
        RIBON_LUCA_RLH1_SECTION_BOOT_CPU,
        &boot_cpu_payload);
    write_u32(
        boot_cpu_payload,
        RIBON_LUCA_RLH1_BOOT_CPU_NAMESPACE_OFFSET,
        2u);
    refresh_checksum(mutated);
    expect(
        ribon_luca_parse_rlh1(mutated, artifact.size, &view) ==
            RIBON_LUCA_RLH1_PARSE_BAD_SECTION,
        "parser rejects unknown BOOT_CPU namespace");

    memcpy(mutated, valid, artifact.size);
    boot_cpu_section = find_section(
        mutated,
        RIBON_LUCA_RLH1_SECTION_BOOT_CPU,
        &boot_cpu_payload);
    write_u64(
        boot_cpu_section,
        RIBON_LUCA_RLH1_SECTION_LENGTH_OFFSET,
        RIBON_LUCA_RLH1_BOOT_CPU_SIZE - 1u);
    refresh_checksum(mutated);
    expect(
        ribon_luca_parse_rlh1(mutated, artifact.size, &view) ==
            RIBON_LUCA_RLH1_PARSE_BAD_SECTION,
        "parser rejects truncated BOOT_CPU payload");

    memcpy(mutated, valid, artifact.size);
    boot_cpu_section = find_section(
        mutated,
        RIBON_LUCA_RLH1_SECTION_BOOT_CPU,
        &boot_cpu_payload);
    write_u32(
        boot_cpu_section,
        RIBON_LUCA_RLH1_SECTION_TYPE_OFFSET,
        0x80000000u);
    write_u32(
        boot_cpu_section,
        RIBON_LUCA_RLH1_SECTION_FLAGS_OFFSET,
        0u);
    refresh_checksum(mutated);
    expect(
        ribon_luca_parse_rlh1(mutated, artifact.size, &view) ==
            RIBON_LUCA_RLH1_PARSE_MISSING_REQUIRED_SECTION,
        "parser rejects RISC-V artifact missing BOOT_CPU");

    expect(
        build_riscv_fixture(
            valid,
            &artifact,
            0u,
            RIBON_BOOT_ENV_HAS_BOOT_CPU_ID |
                RIBON_BOOT_ENV_HAS_DEVICE_TREE) ==
            RIBON_PROTOCOL_HANDOFF_STATUS_OK &&
            ribon_luca_parse_rlh1(valid, artifact.size, &view) ==
                RIBON_LUCA_RLH1_PARSE_OK &&
            view.boot_cpu_id == 0u,
        "bootstrap hart ID zero remains valid");
    expect(
        build_riscv_fixture(
            valid,
            &artifact,
            7u,
            RIBON_BOOT_ENV_HAS_DEVICE_TREE) ==
            RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN,
        "builder rejects RISC-V environment without boot CPU authority");
    expect(
        build_riscv_fixture(
            valid,
            &artifact,
            7u,
            RIBON_BOOT_ENV_HAS_BOOT_CPU_ID) ==
            RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN,
        "builder rejects raw-FDT RISC-V environment without device tree");

    expect(
        build_fixture_with_inputs(
            valid,
            &artifact,
            "protocol=luca",
            14u,
            modules,
            2u) == RIBON_PROTOCOL_HANDOFF_STATUS_OK,
        "builder accepts one initial image and ordered auxiliary module");
    expect(
        ribon_luca_parse_rlh1(valid, artifact.size, &view) ==
            RIBON_LUCA_RLH1_PARSE_OK,
        "parser accepts typed module inventory");
    module_section = find_section(
        valid,
        RIBON_LUCA_RLH1_SECTION_MODULES,
        &module_payload);
    expect(
        module_section != 0 && module_payload != 0 &&
            read_u32(
                module_section,
                RIBON_LUCA_RLH1_SECTION_FLAGS_OFFSET) ==
                (RIBON_LUCA_RLH1_SECTION_REQUIRED_TO_UNDERSTAND |
                 RIBON_LUCA_RLH1_SECTION_BORROWED_RANGE_DESCRIPTOR) &&
            read_u32(module_payload, 0u) == 2u &&
            read_u32(module_payload, 4u) ==
                RIBON_LUCA_RLH1_MODULE_ENTRY_SIZE &&
            read_u64(module_payload + 8u, 0u) == modules[0].physical_address &&
            read_u64(module_payload + 8u, 8u) == modules[0].size &&
            read_u32(module_payload + 8u, 16u) ==
                RIBON_LUCA_RLH1_MODULE_FLAG_INITIAL_IMAGE &&
            read_u32(module_payload + 8u, 20u) == 0u &&
            read_u64(module_payload + 8u, 24u) == 0u &&
            read_u64(
                module_payload + 8u + RIBON_LUCA_RLH1_MODULE_ENTRY_SIZE,
                0u) == modules[1].physical_address &&
            read_u64(
                module_payload + 8u + RIBON_LUCA_RLH1_MODULE_ENTRY_SIZE,
                8u) == modules[1].size &&
            read_u32(
                module_payload + 8u +
                    RIBON_LUCA_RLH1_MODULE_ENTRY_SIZE,
                16u) == 0u &&
            read_u32(
                module_payload + 8u +
                    RIBON_LUCA_RLH1_MODULE_ENTRY_SIZE,
                20u) == 0u &&
            read_u64(
                module_payload + 8u +
                    RIBON_LUCA_RLH1_MODULE_ENTRY_SIZE,
                24u) == 0u,
        "module section preserves order, exact spans, roles, and zero fields");

    expect(
        build_fixture_with_inputs(
            valid, &artifact, "protocol=luca", 14u, &modules[1], 1u) ==
            RIBON_PROTOCOL_HANDOFF_STATUS_OK &&
        ribon_luca_parse_rlh1(valid, artifact.size, &view) ==
            RIBON_LUCA_RLH1_PARSE_OK,
        "builder and parser accept auxiliary-only inventory");
    module_section = find_section(
        valid, RIBON_LUCA_RLH1_SECTION_MODULES, &module_payload);
    expect(
        module_section != 0 && read_u32(module_payload, 0u) == 1u &&
            read_u32(module_payload + 8u, 16u) == 0u,
        "auxiliary-only inventory carries no initial-image bit");
    expect(
        build_fixture_with_inputs(
            valid, &artifact, "protocol=luca", 14u, modules, 2u) ==
            RIBON_PROTOCOL_HANDOFF_STATUS_OK,
        "mixed module fixture restores for malformed corpus");

    memcpy(mutated, valid, artifact.size);
    module_section = find_section(
        mutated, RIBON_LUCA_RLH1_SECTION_MODULES, &module_payload);
    write_u32(module_payload, 0u, 0u);
    refresh_checksum(mutated);
    expect(
        ribon_luca_parse_rlh1(mutated, artifact.size, &view) ==
            RIBON_LUCA_RLH1_PARSE_BAD_SECTION,
        "parser rejects zero module count section");

    memcpy(mutated, valid, artifact.size);
    module_section = find_section(
        mutated, RIBON_LUCA_RLH1_SECTION_MODULES, &module_payload);
    write_u32(module_payload, 0u, RIBON_LUCA_RLH1_MAX_MODULES + 1u);
    refresh_checksum(mutated);
    expect(
        ribon_luca_parse_rlh1(mutated, artifact.size, &view) ==
            RIBON_LUCA_RLH1_PARSE_BAD_SECTION,
        "parser rejects ninth module count");

    memcpy(mutated, valid, artifact.size);
    module_section = find_section(
        mutated,
        RIBON_LUCA_RLH1_SECTION_MODULES,
        &module_payload);
    write_u32(module_payload + 8u, 16u, 2u);
    refresh_checksum(mutated);
    expect(
        ribon_luca_parse_rlh1(mutated, artifact.size, &view) ==
            RIBON_LUCA_RLH1_PARSE_BAD_SECTION,
        "parser rejects unknown module role flag");

    memcpy(mutated, valid, artifact.size);
    module_section = find_section(
        mutated,
        RIBON_LUCA_RLH1_SECTION_MODULES,
        &module_payload);
    write_u32(
        module_payload + 8u + RIBON_LUCA_RLH1_MODULE_ENTRY_SIZE,
        16u,
        RIBON_LUCA_RLH1_MODULE_FLAG_INITIAL_IMAGE);
    refresh_checksum(mutated);
    expect(
        ribon_luca_parse_rlh1(mutated, artifact.size, &view) ==
            RIBON_LUCA_RLH1_PARSE_BAD_SECTION,
        "parser rejects duplicate initial image");

    memcpy(mutated, valid, artifact.size);
    module_section = find_section(
        mutated,
        RIBON_LUCA_RLH1_SECTION_MODULES,
        &module_payload);
    write_u64(module_payload + 8u, 0u, 0u);
    refresh_checksum(mutated);
    expect(
        ribon_luca_parse_rlh1(mutated, artifact.size, &view) ==
            RIBON_LUCA_RLH1_PARSE_BAD_SECTION,
        "parser rejects zero module address");

    memcpy(mutated, valid, artifact.size);
    module_section = find_section(
        mutated, RIBON_LUCA_RLH1_SECTION_MODULES, &module_payload);
    write_u64(module_payload + 8u, 8u, 0u);
    refresh_checksum(mutated);
    expect(
        ribon_luca_parse_rlh1(mutated, artifact.size, &view) ==
            RIBON_LUCA_RLH1_PARSE_BAD_SECTION,
        "parser rejects zero module size");

    memcpy(mutated, valid, artifact.size);
    module_section = find_section(
        mutated, RIBON_LUCA_RLH1_SECTION_MODULES, &module_payload);
    write_u64(module_payload + 8u, 0u, UINT64_MAX - 0x10u);
    write_u64(module_payload + 8u, 8u, 0x20u);
    refresh_checksum(mutated);
    expect(
        ribon_luca_parse_rlh1(mutated, artifact.size, &view) ==
            RIBON_LUCA_RLH1_PARSE_BAD_SECTION,
        "parser rejects wrapping module range");

    memcpy(mutated, valid, artifact.size);
    module_section = find_section(
        mutated,
        RIBON_LUCA_RLH1_SECTION_MODULES,
        &module_payload);
    write_u64(
        module_payload + 8u + RIBON_LUCA_RLH1_MODULE_ENTRY_SIZE,
        0u,
        0x307000u);
    refresh_checksum(mutated);
    expect(
        ribon_luca_parse_rlh1(mutated, artifact.size, &view) ==
            RIBON_LUCA_RLH1_PARSE_BAD_SECTION,
        "parser rejects overlapping modules");

    memcpy(mutated, valid, artifact.size);
    module_section = find_section(
        mutated,
        RIBON_LUCA_RLH1_SECTION_MODULES,
        &module_payload);
    write_u64(module_payload + 8u, 0u, 0x200000u);
    refresh_checksum(mutated);
    expect(
        ribon_luca_parse_rlh1(mutated, artifact.size, &view) ==
            RIBON_LUCA_RLH1_PARSE_BAD_SECTION,
        "parser rejects module and kernel overlap");

    memcpy(mutated, valid, artifact.size);
    module_section = find_section(
        mutated,
        RIBON_LUCA_RLH1_SECTION_MODULES,
        &module_payload);
    write_u64(module_payload + 8u, 24u, 1u);
    refresh_checksum(mutated);
    expect(
        ribon_luca_parse_rlh1(mutated, artifact.size, &view) ==
            RIBON_LUCA_RLH1_PARSE_BAD_SECTION,
        "parser rejects nonzero reserved name digest");

    expect(
        build_fixture(valid, &artifact) == RIBON_PROTOCOL_HANDOFF_STATUS_OK,
        "builder restores command-only fixture");
    total_size =
        read_u32(valid, RIBON_LUCA_RLH1_HEADER_TOTAL_SIZE_OFFSET);
    table_offset =
        read_u32(valid, RIBON_LUCA_RLH1_HEADER_SECTION_TABLE_OFFSET);
    first_payload = read_u64(
        valid + table_offset,
        RIBON_LUCA_RLH1_SECTION_PAYLOAD_OFFSET);

    memcpy(mutated, valid, total_size);
    mutated[first_payload] ^= 0x1u;
    expect(ribon_luca_parse_rlh1(mutated, total_size, &view) ==
               RIBON_LUCA_RLH1_PARSE_BAD_CHECKSUM,
           "parser rejects payload corruption");

    memcpy(mutated, valid, total_size);
    mutated[RIBON_LUCA_RLH1_HEADER_VERSION_MAJOR_OFFSET] = 2u;
    refresh_checksum(mutated);
    expect(ribon_luca_parse_rlh1(mutated, total_size, &view) ==
               RIBON_LUCA_RLH1_PARSE_UNSUPPORTED_VERSION,
           "parser rejects unsupported major version");

    memcpy(mutated, valid, total_size);
    write_u32(
        mutated + table_offset,
        RIBON_LUCA_RLH1_SECTION_TYPE_OFFSET,
        0x80000000u);
    refresh_checksum(mutated);
    expect(ribon_luca_parse_rlh1(mutated, total_size, &view) ==
               RIBON_LUCA_RLH1_PARSE_UNKNOWN_REQUIRED_SECTION,
           "parser rejects unknown required section");

    memcpy(mutated, valid, total_size);
    write_u32(
        mutated + table_offset + RIBON_LUCA_RLH1_SECTION_ENTRY_SIZE,
        RIBON_LUCA_RLH1_SECTION_TYPE_OFFSET,
        RIBON_LUCA_RLH1_SECTION_MEMORY_MAP);
    refresh_checksum(mutated);
    expect(ribon_luca_parse_rlh1(mutated, total_size, &view) ==
               RIBON_LUCA_RLH1_PARSE_DUPLICATE_SECTION,
           "parser rejects singleton duplication");

    memcpy(mutated, valid, total_size);
    write_u64(
        mutated + table_offset + RIBON_LUCA_RLH1_SECTION_ENTRY_SIZE,
        RIBON_LUCA_RLH1_SECTION_PAYLOAD_OFFSET,
        first_payload);
    refresh_checksum(mutated);
    expect(ribon_luca_parse_rlh1(mutated, total_size, &view) ==
               RIBON_LUCA_RLH1_PARSE_BAD_SECTION,
           "parser rejects overlapping payloads");

    memcpy(mutated, valid, total_size);
    mutated[RIBON_LUCA_RLH1_HEADER_RESERVED0_OFFSET] = 1u;
    expect(ribon_luca_parse_rlh1(mutated, total_size, &view) ==
               RIBON_LUCA_RLH1_PARSE_BAD_HEADER,
           "parser rejects nonzero reserved header fields");

    memcpy(mutated, valid, total_size);
    write_u32(
        mutated,
        RIBON_LUCA_RLH1_HEADER_SECTION_TABLE_OFFSET,
        table_offset + 1u);
    refresh_checksum(mutated);
    expect(ribon_luca_parse_rlh1(mutated, total_size, &view) ==
               RIBON_LUCA_RLH1_PARSE_BAD_HEADER,
           "parser rejects misaligned section table");

    memset(maximum_command_line,
           'x',
           RIBON_LUCA_RLH1_COMMAND_LINE_MAX_SIZE);
    maximum_command_line[
        RIBON_LUCA_RLH1_COMMAND_LINE_MAX_SIZE - 1u] = '\0';
    maximum_command_line[
        RIBON_LUCA_RLH1_COMMAND_LINE_MAX_SIZE] = '\0';
    expect(
        build_fixture_with_command(
            valid,
            &artifact,
            maximum_command_line,
            RIBON_LUCA_RLH1_COMMAND_LINE_MAX_SIZE - 1u) ==
            RIBON_PROTOCOL_HANDOFF_STATUS_OK,
        "builder accepts command line at the RLH1 v1 maximum");
    expect(
        ribon_luca_parse_rlh1(valid, artifact.size, &view) ==
            RIBON_LUCA_RLH1_PARSE_OK,
        "parser accepts command line at the RLH1 v1 maximum");
    table_offset =
        read_u32(valid, RIBON_LUCA_RLH1_HEADER_SECTION_TABLE_OFFSET);
    write_u64(
        valid + table_offset +
            (3u * RIBON_LUCA_RLH1_SECTION_ENTRY_SIZE),
        RIBON_LUCA_RLH1_SECTION_LENGTH_OFFSET,
        RIBON_LUCA_RLH1_COMMAND_LINE_MAX_SIZE + 1u);
    refresh_checksum(valid);
    expect(
        ribon_luca_parse_rlh1(valid, artifact.size, &view) ==
            RIBON_LUCA_RLH1_PARSE_BAD_SECTION,
        "parser rejects command line above the RLH1 v1 maximum");
    expect(
        build_fixture_with_command(
            valid,
            &artifact,
            maximum_command_line,
            RIBON_LUCA_RLH1_COMMAND_LINE_MAX_SIZE) ==
            RIBON_PROTOCOL_HANDOFF_STATUS_OUT_OF_CAPACITY,
        "builder rejects command line above the RLH1 v1 maximum");

    if (failures != 0) {
        return 1;
    }
    puts("RLH1-TEST-OK");
    return 0;
}
