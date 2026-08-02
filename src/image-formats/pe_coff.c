#include <Ribon/boot/image.h>
#include <Ribon/plugin/descriptor.h>

#include <stddef.h>

#define RIBON_PE_SIGNATURE_OFFSET_FIELD 0x3cu
#define RIBON_PE_COFF_HEADER_SIZE 20u
#define RIBON_PE32_PLUS_MAGIC 0x20bu
#define RIBON_PE32_PLUS_OPTIONAL_MINIMUM 112u
#define RIBON_PE_SECTION_HEADER_SIZE 40u
#define RIBON_PE_SCN_MEM_EXECUTE 0x20000000u
#define RIBON_PE_SCN_MEM_READ 0x40000000u
#define RIBON_PE_SCN_MEM_WRITE 0x80000000u
#define RIBON_PE_MAX_SECTIONS 96u

/** @brief Little-endian 16-bit field를 unaligned byte에서 읽는다. */
static uint16_t pe_read_u16(const unsigned char *bytes) {
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8u);
}

/** @brief Little-endian 32-bit field를 unaligned byte에서 읽는다. */
static uint32_t pe_read_u32(const unsigned char *bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) |
           ((uint32_t)bytes[3] << 24u);
}

/** @brief Little-endian 64-bit field를 unaligned byte에서 읽는다. */
static uint64_t pe_read_u64(const unsigned char *bytes) {
    return (uint64_t)pe_read_u32(bytes) |
           ((uint64_t)pe_read_u32(bytes + 4u) << 32u);
}

/** @brief 두 unsigned 값을 overflow 없이 더한다. */
static int pe_add(uint64_t lhs, uint64_t rhs, uint64_t *out) {
    if (out == 0 || lhs > UINT64_MAX - rhs) {
        return 0;
    }
    *out = lhs + rhs;
    return 1;
}

/** @brief PE section characteristic을 Ribon load permission으로 변환한다. */
static uint32_t pe_segment_flags(uint32_t characteristics) {
    uint32_t flags = 0u;
    if ((characteristics & RIBON_PE_SCN_MEM_READ) != 0u) {
        flags |= RIBON_LOAD_SEGMENT_READ;
    }
    if ((characteristics & RIBON_PE_SCN_MEM_WRITE) != 0u) {
        flags |= RIBON_LOAD_SEGMENT_WRITE;
    }
    if ((characteristics & RIBON_PE_SCN_MEM_EXECUTE) != 0u) {
        flags |= RIBON_LOAD_SEGMENT_EXECUTE;
    }
    return flags;
}

/**
 * @brief PE32+ image를 caller-owned segment array에 분석한다.
 *
 * Relocation과 import resolution은 수행하지 않으며 preferred image base만 허용한다.
 */
static int pe_coff_analyze(
    const struct RibonPayloadImage *image,
    struct RibonValidatedImage *validated_out,
    struct RibonDirectLoadPlan *out) {
    const unsigned char *bytes;
    uint32_t pe_offset;
    uint16_t machine;
    uint16_t section_count;
    uint16_t optional_size;
    const unsigned char *optional;
    uint32_t entry_rva;
    uint64_t image_base;
    uint32_t section_alignment;
    uint64_t section_table_offset;
    uint64_t section_table_end;
    uint64_t image_end = 0u;
    uint64_t image_start = UINT64_MAX;
    uint32_t emitted_segments = 0u;
    int entry_seen = 0;

    if (image == 0 || image->data == 0 || validated_out == 0 ||
        (out != 0 && !ribon_direct_load_plan_has_storage(out)) ||
        image->size < RIBON_PE_SIGNATURE_OFFSET_FIELD + 4u) {
        return RIBON_LOADER_STATUS_BAD_ARGUMENT;
    }
    *validated_out = (struct RibonValidatedImage){0};
    bytes = (const unsigned char *)image->data;
    if (bytes[0] != 'M' || bytes[1] != 'Z') {
        return RIBON_LOADER_STATUS_BAD_FORMAT;
    }
    pe_offset = pe_read_u32(bytes + RIBON_PE_SIGNATURE_OFFSET_FIELD);
    if ((uint64_t)pe_offset + 4u + RIBON_PE_COFF_HEADER_SIZE > image->size) {
        return RIBON_LOADER_STATUS_TRUNCATED;
    }
    if (bytes[pe_offset] != 'P' || bytes[pe_offset + 1u] != 'E' ||
        bytes[pe_offset + 2u] != 0u || bytes[pe_offset + 3u] != 0u) {
        return RIBON_LOADER_STATUS_BAD_FORMAT;
    }
    machine = pe_read_u16(bytes + pe_offset + 4u);
    section_count = pe_read_u16(bytes + pe_offset + 6u);
    optional_size = pe_read_u16(bytes + pe_offset + 20u);
    if (machine == 0u) {
        return RIBON_LOADER_STATUS_UNSUPPORTED;
    }
    if (section_count == 0u || section_count > RIBON_PE_MAX_SECTIONS ||
        (out != 0 && section_count > out->segment_capacity) ||
        optional_size < RIBON_PE32_PLUS_OPTIONAL_MINIMUM) {
        return RIBON_LOADER_STATUS_BAD_FORMAT;
    }
    section_table_offset =
        (uint64_t)pe_offset + 4u + RIBON_PE_COFF_HEADER_SIZE + optional_size;
    if (!pe_add(
            section_table_offset,
            (uint64_t)section_count * RIBON_PE_SECTION_HEADER_SIZE,
            &section_table_end) ||
        section_table_end > image->size) {
        return RIBON_LOADER_STATUS_TRUNCATED;
    }
    optional = bytes + pe_offset + 4u + RIBON_PE_COFF_HEADER_SIZE;
    if (pe_read_u16(optional) != RIBON_PE32_PLUS_MAGIC) {
        return RIBON_LOADER_STATUS_UNSUPPORTED;
    }
    entry_rva = pe_read_u32(optional + 16u);
    image_base = pe_read_u64(optional + 24u);
    section_alignment = pe_read_u32(optional + 32u);
    if (entry_rva == 0u || (out != 0 && image_base == 0u) ||
        section_alignment == 0u ||
        (section_alignment & (section_alignment - 1u)) != 0u) {
        return RIBON_LOADER_STATUS_BAD_FORMAT;
    }

    if (out != 0) {
        struct RibonLoadSegment *segments = out->segments;
        const uint32_t segment_capacity = out->segment_capacity;
        *out = (struct RibonDirectLoadPlan){
            .size = sizeof(*out),
            .abi_version = RIBON_DIRECT_LOAD_PLAN_ABI_VERSION,
            .segments = segments,
            .segment_capacity = segment_capacity,
            .linked_virtual_base = UINT64_MAX,
            .linked_physical_base = UINT64_MAX,
            .load_base = UINT64_MAX,
            .runtime_load_base = UINT64_MAX,
        };
    }
    for (uint32_t index = 0u; index < section_count; ++index) {
        const unsigned char *section =
            bytes + section_table_offset +
            ((uint64_t)index * RIBON_PE_SECTION_HEADER_SIZE);
        uint64_t virtual_size = pe_read_u32(section + 8u);
        const uint64_t virtual_address = pe_read_u32(section + 12u);
        const uint64_t raw_size = pe_read_u32(section + 16u);
        const uint64_t raw_offset = pe_read_u32(section + 20u);
        const uint32_t characteristics = pe_read_u32(section + 36u);
        uint64_t raw_end;
        uint64_t segment_base;
        uint64_t segment_end;
        struct RibonLoadSegment *segment;

        if (virtual_size == 0u) {
            virtual_size = raw_size;
        }
        if (virtual_size == 0u) {
            continue;
        }
        if (!pe_add(raw_offset, raw_size, &raw_end) || raw_end > image->size ||
            (out != 0 && raw_size > virtual_size) ||
            !pe_add(image_base, virtual_address, &segment_base) ||
            !pe_add(segment_base, virtual_size, &segment_end) ||
            (out != 0 && (virtual_address % section_alignment) != 0u)) {
            return RIBON_LOADER_STATUS_BAD_FORMAT;
        }
        for (uint32_t previous = 0u; previous < index; ++previous) {
            const unsigned char *previous_section =
                bytes + section_table_offset +
                ((uint64_t)previous * RIBON_PE_SECTION_HEADER_SIZE);
            uint64_t previous_size = pe_read_u32(previous_section + 8u);
            const uint64_t previous_raw_size = pe_read_u32(previous_section + 16u);
            const uint64_t previous_virtual_address = pe_read_u32(previous_section + 12u);
            uint64_t previous_base;
            uint64_t previous_end;
            if (previous_size == 0u) {
                previous_size = previous_raw_size;
            }
            if (previous_size == 0u) {
                continue;
            }
            if (!pe_add(image_base, previous_virtual_address, &previous_base) ||
                !pe_add(previous_base, previous_size, &previous_end) ||
                (segment_base < previous_end && previous_base < segment_end)) {
                return RIBON_LOADER_STATUS_OVERLAPPING_SEGMENTS;
            }
        }
        if (out != 0) {
            segment = &out->segments[out->segment_count++];
            *segment = (struct RibonLoadSegment){
                .file_offset = raw_offset,
                .file_size = raw_size,
                .memory_size = virtual_size,
                .virtual_address = segment_base,
                .linked_physical_address = segment_base,
                .physical_address = segment_base,
                .load_address = segment_base,
                .runtime_address = segment_base,
                .alignment = section_alignment,
                .flags = pe_segment_flags(characteristics),
            };
        }
        ++emitted_segments;
        if (segment_base < image_start) {
            image_start = segment_base;
        }
        if (segment_end > image_end) {
            image_end = segment_end;
        }
        if ((uint64_t)entry_rva >= virtual_address &&
            (uint64_t)entry_rva < virtual_address + virtual_size &&
            (pe_segment_flags(characteristics) & RIBON_LOAD_SEGMENT_EXECUTE) != 0u) {
            entry_seen = 1;
        }
    }
    if (emitted_segments == 0u || !entry_seen || image_end <= image_start) {
        return RIBON_LOADER_STATUS_NO_LOAD_SEGMENTS;
    }
    if (out != 0) {
        out->entry_point = image_base + entry_rva;
        out->entry_load_address = out->entry_point;
        out->runtime_entry_address = out->entry_point;
        out->load_base = image_start;
        out->runtime_load_base = image_start;
        out->linked_virtual_base = image_start;
        out->linked_physical_base = image_start;
        out->load_end = image_end;
        out->runtime_load_end = image_end;
        out->linked_virtual_end = image_end;
        out->linked_physical_end = image_end;
        out->memory_size = image_end - image_start;
        out->load_plan_flags =
            RIBON_LOAD_PLAN_ENTRY_LOAD_VALID |
            RIBON_LOAD_PLAN_RUNTIME_ENTRY_VALID |
            RIBON_LOAD_PLAN_USES_PADDR |
            RIBON_LOAD_PLAN_HAS_LINKED_PHYSICAL_RANGE;
    }
    *validated_out = (struct RibonValidatedImage){
        .size = sizeof(*validated_out),
        .abi_version = RIBON_VALIDATED_IMAGE_ABI_VERSION,
        .format = RIBON_EXECUTABLE_FORMAT_PE_COFF,
        .machine = machine,
        .execution_support =
            RIBON_IMAGE_EXECUTION_DIRECT_ENTRY |
            RIBON_IMAGE_EXECUTION_FIRMWARE_MANAGED,
        .image_size = image->size,
        .validation_receipt = ((uint64_t)section_count << 32u) | entry_rva,
    };
    return RIBON_LOADER_STATUS_OK;
}

static const struct RibonImageFormatOps pe_coff_ops = {
    .size = sizeof(pe_coff_ops),
    .abi_version = RIBON_IMAGE_FORMAT_OPS_ABI_VERSION,
    .format = RIBON_EXECUTABLE_FORMAT_PE_COFF,
    .execution_support =
        RIBON_IMAGE_EXECUTION_DIRECT_ENTRY |
        RIBON_IMAGE_EXECUTION_FIRMWARE_MANAGED,
    .analyze = pe_coff_analyze,
};

/** @brief PE32+ image-format plugin descriptor다. */
const struct RibonPluginDescriptor ribon_pe_coff_image_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_pe_coff_image_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_IMAGE_FORMAT,
    .phase = RIBON_PLUGIN_PHASE_BOOT,
    .id = "image.pe-coff",
    .provides = RIBON_CAP_IMAGE_PE_COFF,
    .requires = 0u,
    .architecture_mask = RIBON_ARCH_MASK_ALL,
    .environment_mask = RIBON_ENV_MASK_ALL,
    .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 4096u,
    .input_budget = 64ull * 1024ull * 1024ull,
    .output_budget = 4096u,
    .deadline_ms = 30000u,
    .operations = &pe_coff_ops,
    .operations_size = sizeof(pe_coff_ops),
    .operations_abi = RIBON_IMAGE_FORMAT_OPS_ABI_VERSION,
    .validate_operations = ribon_image_plugin_operations_are_valid,
};
