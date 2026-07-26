#include <Ribon/arch/ops.h>
#include <Ribon/boot/image.h>
#include <Ribon/plugin/descriptor.h>

#include <stddef.h>
#include <string.h>

#define RIBON_ELF64_HEADER_SIZE 64u
#define RIBON_ELF64_PHDR_SIZE 56u
#define RIBON_ELF_CLASS_64 2u
#define RIBON_ELF_DATA_LSB 1u
#define RIBON_ELF_VERSION_CURRENT 1u
#define RIBON_ELF_TYPE_EXEC 2u
#define RIBON_ELF_TYPE_DYN 3u
#define RIBON_ELF_PHDR_LOAD 1u
#define RIBON_ELF_PF_X 1u
#define RIBON_ELF_PF_W 2u
#define RIBON_ELF_PF_R 4u
#define RIBON_ELF_MACHINE_X86_64 62u
#define RIBON_ELF_MACHINE_AARCH64 183u
#define RIBON_ELF_MACHINE_RISCV 243u

static uint16_t ribon_read_le16(const unsigned char *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t ribon_read_le32(const unsigned char *data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint64_t ribon_read_le64(const unsigned char *data) {
    return (uint64_t)ribon_read_le32(data) | ((uint64_t)ribon_read_le32(data + 4) << 32);
}

static int ribon_u64_add(uint64_t lhs, uint64_t rhs, uint64_t *out) {
    if (out == 0) {
        return RIBON_LOADER_STATUS_BAD_ARGUMENT;
    }
    if (lhs > UINT64_MAX - rhs) {
        return RIBON_LOADER_STATUS_OVERFLOW;
    }
    *out = lhs + rhs;
    return RIBON_LOADER_STATUS_OK;
}

static int ribon_u64_mul(uint64_t lhs, uint64_t rhs, uint64_t *out) {
    if (out == 0) {
        return RIBON_LOADER_STATUS_BAD_ARGUMENT;
    }
    if (lhs != 0u && rhs > UINT64_MAX / lhs) {
        return RIBON_LOADER_STATUS_OVERFLOW;
    }
    *out = lhs * rhs;
    return RIBON_LOADER_STATUS_OK;
}

static int ribon_is_power_of_two(uint64_t value) {
    return value != 0u && (value & (value - 1u)) == 0u;
}

static int ribon_machine_for_arch(const struct RibonArchDescriptor *arch, uint16_t *out) {
    if (arch == 0 || arch->canonical_name == 0 || out == 0) {
        return RIBON_LOADER_STATUS_BAD_ARGUMENT;
    }
    if (strcmp(arch->canonical_name, "x86_64") == 0) {
        *out = RIBON_ELF_MACHINE_X86_64;
        return RIBON_LOADER_STATUS_OK;
    }
    if (strcmp(arch->canonical_name, "aarch64") == 0) {
        *out = RIBON_ELF_MACHINE_AARCH64;
        return RIBON_LOADER_STATUS_OK;
    }
    if (strcmp(arch->canonical_name, "riscv64") == 0) {
        *out = RIBON_ELF_MACHINE_RISCV;
        return RIBON_LOADER_STATUS_OK;
    }
    return RIBON_LOADER_STATUS_UNSUPPORTED;
}

static int ribon_address_fits(uint64_t address, uint32_t bits) {
    if (bits >= 64u) {
        return 1;
    }
    if (bits == 0u) {
        return address == 0u;
    }
    return address < (1ull << bits);
}

static int ribon_address_is_canonical(uint64_t address, uint32_t bits) {
    uint64_t sign_bit;
    uint64_t upper_mask;
    if (bits >= 64u) {
        return 1;
    }
    if (bits == 0u) {
        return address == 0u;
    }
    sign_bit = 1ull << (bits - 1u);
    upper_mask = UINT64_MAX << bits;
    if ((address & sign_bit) != 0u) {
        return (address & upper_mask) == upper_mask;
    }
    return (address & upper_mask) == 0u;
}

static int ribon_address_is_high_half(uint64_t address, uint32_t bits) {
    if (bits == 0u || bits >= 64u || !ribon_address_is_canonical(address, bits)) {
        return 0;
    }
    return (address & (1ull << (bits - 1u))) != 0u;
}

static uint32_t ribon_segment_flags_from_elf(uint32_t flags) {
    uint32_t mapped = 0;
    if ((flags & RIBON_ELF_PF_R) != 0u) {
        mapped |= RIBON_LOAD_SEGMENT_READ;
    }
    if ((flags & RIBON_ELF_PF_W) != 0u) {
        mapped |= RIBON_LOAD_SEGMENT_WRITE;
    }
    if ((flags & RIBON_ELF_PF_X) != 0u) {
        mapped |= RIBON_LOAD_SEGMENT_EXECUTE;
    }
    return mapped;
}

static int ribon_ranges_overlap(uint64_t lhs_base, uint64_t lhs_end, uint64_t rhs_base, uint64_t rhs_end) {
    return lhs_base < rhs_end && rhs_base < lhs_end;
}

static void ribon_loaded_payload_reset(struct RibonLoadedPayload *out) {
    out->format = RIBON_EXECUTABLE_FORMAT_UNKNOWN;
    out->machine = 0;
    out->segment_count = 0;
    out->load_plan_flags = 0;
    out->entry_point = 0;
    out->entry_load_address = 0;
    out->runtime_entry_address = 0;
    out->load_base = 0;
    out->load_end = 0;
    out->runtime_load_base = 0;
    out->runtime_load_end = 0;
    out->memory_size = 0;
    out->linked_virtual_base = 0;
    out->linked_virtual_end = 0;
    out->linked_physical_base = 0;
    out->linked_physical_end = 0;
    out->high_entry_virtual_address = 0;
    out->high_entry_load_address = 0;
}

const char *ribon_executable_format_name(enum RibonExecutableFormat format) {
    switch (format) {
    case RIBON_EXECUTABLE_FORMAT_ELF64:
        return "elf64";
    case RIBON_EXECUTABLE_FORMAT_UNKNOWN:
    default:
        return "unknown";
    }
}

const char *ribon_loader_status_name(enum RibonLoaderStatus status) {
    switch (status) {
    case RIBON_LOADER_STATUS_OK:
        return "ok";
    case RIBON_LOADER_STATUS_BAD_ARGUMENT:
        return "bad-argument";
    case RIBON_LOADER_STATUS_BAD_FORMAT:
        return "bad-format";
    case RIBON_LOADER_STATUS_UNSUPPORTED:
        return "unsupported";
    case RIBON_LOADER_STATUS_TRUNCATED:
        return "truncated";
    case RIBON_LOADER_STATUS_OVERFLOW:
        return "overflow";
    case RIBON_LOADER_STATUS_OUT_OF_CAPACITY:
        return "out-of-capacity";
    case RIBON_LOADER_STATUS_MISALIGNED:
        return "misaligned";
    case RIBON_LOADER_STATUS_NO_LOAD_SEGMENTS:
        return "no-load-segments";
    case RIBON_LOADER_STATUS_NON_CANONICAL:
        return "non-canonical";
    case RIBON_LOADER_STATUS_OVERLAPPING_SEGMENTS:
        return "overlapping-segments";
    default:
        return "unknown";
    }
}

static int elf64_analyze(
    const struct RibonPayloadImage *image,
    const struct RibonArchDescriptor *arch,
    struct RibonLoadedPayload *out) {
    const unsigned char *data;
    uint16_t expected_machine = 0;
    uint16_t e_type;
    uint16_t e_machine;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t phdr_table_size = 0;
    uint64_t phdr_table_end = 0;
    uint64_t load_base = UINT64_MAX;
    uint64_t load_end = 0;
    uint64_t linked_virtual_base = UINT64_MAX;
    uint64_t linked_virtual_end = 0;
    uint64_t linked_physical_base = UINT64_MAX;
    uint64_t linked_physical_end = 0;
    uint64_t entry_load_address = 0;
    uint64_t high_entry_virtual_address = 0;
    uint64_t high_entry_load_address = 0;
    uint32_t load_plan_flags = 0;
    int entry_is_loadable = 0;
    uint16_t index;

    if (image == 0 || image->data == 0 || arch == 0 || out == 0 || out->segments == 0 ||
        out->segment_capacity == 0u) {
        return RIBON_LOADER_STATUS_BAD_ARGUMENT;
    }
    ribon_loaded_payload_reset(out);
    data = (const unsigned char *)image->data;
    if (image->size < RIBON_ELF64_HEADER_SIZE) {
        return RIBON_LOADER_STATUS_TRUNCATED;
    }
    if (data[0] != 0x7fu || data[1] != 'E' || data[2] != 'L' || data[3] != 'F') {
        return RIBON_LOADER_STATUS_BAD_FORMAT;
    }
    if (data[4] != RIBON_ELF_CLASS_64 || data[5] != RIBON_ELF_DATA_LSB ||
        data[6] != RIBON_ELF_VERSION_CURRENT) {
        return RIBON_LOADER_STATUS_UNSUPPORTED;
    }

    e_type = ribon_read_le16(data + 16);
    e_machine = ribon_read_le16(data + 18);
    e_version = ribon_read_le32(data + 20);
    e_entry = ribon_read_le64(data + 24);
    e_phoff = ribon_read_le64(data + 32);
    e_ehsize = ribon_read_le16(data + 52);
    e_phentsize = ribon_read_le16(data + 54);
    e_phnum = ribon_read_le16(data + 56);

    if (e_type != RIBON_ELF_TYPE_EXEC && e_type != RIBON_ELF_TYPE_DYN) {
        return RIBON_LOADER_STATUS_UNSUPPORTED;
    }
    if (e_version != RIBON_ELF_VERSION_CURRENT || e_ehsize != RIBON_ELF64_HEADER_SIZE ||
        e_phentsize != RIBON_ELF64_PHDR_SIZE || e_phnum == 0u) {
        return RIBON_LOADER_STATUS_BAD_FORMAT;
    }
    if (ribon_machine_for_arch(arch, &expected_machine) != RIBON_LOADER_STATUS_OK ||
        e_machine != expected_machine) {
        return RIBON_LOADER_STATUS_UNSUPPORTED;
    }
    if (ribon_u64_mul(e_phentsize, e_phnum, &phdr_table_size) != RIBON_LOADER_STATUS_OK ||
        ribon_u64_add(e_phoff, phdr_table_size, &phdr_table_end) != RIBON_LOADER_STATUS_OK) {
        return RIBON_LOADER_STATUS_OVERFLOW;
    }
    if (phdr_table_end > image->size) {
        return RIBON_LOADER_STATUS_TRUNCATED;
    }
    if (!ribon_address_is_canonical(e_entry, arch->virtual_address_bits)) {
        return RIBON_LOADER_STATUS_NON_CANONICAL;
    }

    for (index = 0; index < e_phnum; ++index) {
        const unsigned char *phdr = data + e_phoff + ((uint64_t)index * e_phentsize);
        const uint32_t p_type = ribon_read_le32(phdr + 0);
        const uint32_t p_flags = ribon_read_le32(phdr + 4);
        const uint64_t p_offset = ribon_read_le64(phdr + 8);
        const uint64_t p_vaddr = ribon_read_le64(phdr + 16);
        const uint64_t p_paddr = ribon_read_le64(phdr + 24);
        const uint64_t p_filesz = ribon_read_le64(phdr + 32);
        const uint64_t p_memsz = ribon_read_le64(phdr + 40);
        const uint64_t p_align = ribon_read_le64(phdr + 48);
        uint64_t file_end = 0;
        uint64_t load_address = p_paddr != 0u ? p_paddr : p_vaddr;
        uint64_t physical_end = 0;
        uint64_t virtual_end = 0;
        struct RibonLoadSegment *segment;

        if (p_type != RIBON_ELF_PHDR_LOAD) {
            continue;
        }
        if (p_memsz == 0u || p_filesz > p_memsz) {
            return RIBON_LOADER_STATUS_BAD_FORMAT;
        }
        if (p_align != 0u && p_align != 1u && !ribon_is_power_of_two(p_align)) {
            return RIBON_LOADER_STATUS_MISALIGNED;
        }
        if (p_align > 1u && (p_offset % p_align) != (p_vaddr % p_align)) {
            return RIBON_LOADER_STATUS_MISALIGNED;
        }
        if (p_align > 1u && (p_offset % p_align) != (load_address % p_align)) {
            return RIBON_LOADER_STATUS_MISALIGNED;
        }
        if (arch->page_size != 0u && (load_address % arch->page_size) != 0u) {
            return RIBON_LOADER_STATUS_MISALIGNED;
        }
        if (ribon_u64_add(p_offset, p_filesz, &file_end) != RIBON_LOADER_STATUS_OK ||
            ribon_u64_add(load_address, p_memsz, &physical_end) != RIBON_LOADER_STATUS_OK ||
            ribon_u64_add(p_vaddr, p_memsz, &virtual_end) != RIBON_LOADER_STATUS_OK) {
            return RIBON_LOADER_STATUS_OVERFLOW;
        }
        if (file_end > image->size) {
            return RIBON_LOADER_STATUS_TRUNCATED;
        }
        if (!ribon_address_is_canonical(p_vaddr, arch->virtual_address_bits) ||
            !ribon_address_is_canonical(virtual_end - 1u, arch->virtual_address_bits)) {
            return RIBON_LOADER_STATUS_NON_CANONICAL;
        }
        if (!ribon_address_fits(physical_end - 1u, arch->physical_address_bits)) {
            return RIBON_LOADER_STATUS_UNSUPPORTED;
        }
        for (uint32_t previous_index = 0; previous_index < out->segment_count; ++previous_index) {
            const struct RibonLoadSegment *previous = &out->segments[previous_index];
            uint64_t previous_end = 0;
            if (ribon_u64_add(previous->load_address, previous->memory_size, &previous_end) !=
                RIBON_LOADER_STATUS_OK) {
                return RIBON_LOADER_STATUS_OVERFLOW;
            }
            if (ribon_ranges_overlap(
                    previous->load_address,
                    previous_end,
                    load_address,
                    physical_end)) {
                return RIBON_LOADER_STATUS_OVERLAPPING_SEGMENTS;
            }
        }
        if (out->segment_count >= out->segment_capacity) {
            return RIBON_LOADER_STATUS_OUT_OF_CAPACITY;
        }

        segment = &out->segments[out->segment_count];
        segment->file_offset = p_offset;
        segment->file_size = p_filesz;
        segment->memory_size = p_memsz;
        segment->virtual_address = p_vaddr;
        segment->linked_physical_address = p_paddr;
        segment->physical_address = load_address;
        segment->load_address = load_address;
        segment->runtime_address = load_address;
        segment->alignment = p_align;
        segment->flags = ribon_segment_flags_from_elf(p_flags);
        ++out->segment_count;

        if (load_address < load_base) {
            load_base = load_address;
        }
        if (physical_end > load_end) {
            load_end = physical_end;
        }
        if (p_vaddr < linked_virtual_base) {
            linked_virtual_base = p_vaddr;
        }
        if (virtual_end > linked_virtual_end) {
            linked_virtual_end = virtual_end;
        }
        if (p_paddr != 0u) {
            uint64_t raw_physical_end = 0;
            if (ribon_u64_add(p_paddr, p_memsz, &raw_physical_end) != RIBON_LOADER_STATUS_OK) {
                return RIBON_LOADER_STATUS_OVERFLOW;
            }
            load_plan_flags |= RIBON_LOAD_PLAN_USES_PADDR | RIBON_LOAD_PLAN_HAS_LINKED_PHYSICAL_RANGE;
            if (p_paddr < linked_physical_base) {
                linked_physical_base = p_paddr;
            }
            if (raw_physical_end > linked_physical_end) {
                linked_physical_end = raw_physical_end;
            }
        }
        if (ribon_address_is_high_half(p_vaddr, arch->virtual_address_bits)) {
            load_plan_flags |= RIBON_LOAD_PLAN_HAS_HIGHER_HALF | RIBON_LOAD_PLAN_DIRECT_HIGH_ENTRY_CANDIDATE;
            if (high_entry_virtual_address == 0u || p_vaddr < high_entry_virtual_address) {
                high_entry_virtual_address = p_vaddr;
                high_entry_load_address = load_address;
            }
        }
        if (e_entry >= p_vaddr && e_entry < virtual_end &&
            (segment->flags & RIBON_LOAD_SEGMENT_EXECUTE) != 0u) {
            entry_is_loadable = 1;
            entry_load_address = load_address + (e_entry - p_vaddr);
        }
    }

    if (out->segment_count == 0u) {
        return RIBON_LOADER_STATUS_NO_LOAD_SEGMENTS;
    }
    if (!entry_is_loadable) {
        return RIBON_LOADER_STATUS_BAD_FORMAT;
    }
    if (load_end < load_base) {
        return RIBON_LOADER_STATUS_OVERFLOW;
    }

    out->format = RIBON_EXECUTABLE_FORMAT_ELF64;
    out->machine = e_machine;
    out->load_plan_flags =
        load_plan_flags |
        RIBON_LOAD_PLAN_ENTRY_LOAD_VALID |
        RIBON_LOAD_PLAN_RUNTIME_ENTRY_VALID;
    out->entry_point = e_entry;
    out->entry_load_address = entry_load_address;
    out->runtime_entry_address = entry_load_address;
    out->load_base = load_base;
    out->load_end = load_end;
    out->runtime_load_base = load_base;
    out->runtime_load_end = load_end;
    out->memory_size = load_end - load_base;
    out->linked_virtual_base = linked_virtual_base;
    out->linked_virtual_end = linked_virtual_end;
    out->linked_physical_base = linked_physical_base == UINT64_MAX ? 0u : linked_physical_base;
    out->linked_physical_end = linked_physical_end;
    out->high_entry_virtual_address = high_entry_virtual_address;
    out->high_entry_load_address = high_entry_load_address;
    return RIBON_LOADER_STATUS_OK;
}

/** @brief Image-format operation table의 ABI와 callback을 검사한다. */
int ribon_image_format_ops_are_valid(const struct RibonImageFormatOps *ops) {
    return ops != 0 &&
           ops->size == sizeof(*ops) &&
           ops->abi_version == RIBON_IMAGE_FORMAT_OPS_ABI_VERSION &&
           ops->format != RIBON_EXECUTABLE_FORMAT_UNKNOWN &&
           ops->analyze != 0;
}

static const struct RibonImageFormatOps elf64_ops = {
    .size = sizeof(elf64_ops),
    .abi_version = RIBON_IMAGE_FORMAT_OPS_ABI_VERSION,
    .format = RIBON_EXECUTABLE_FORMAT_ELF64,
    .analyze = elf64_analyze,
};

/** @brief Image-format plugin descriptor와 operation table을 함께 검사한다. */
int ribon_image_plugin_operations_are_valid(
    const struct RibonPluginDescriptor *descriptor) {
    const struct RibonImageFormatOps *ops;
    if (descriptor == 0 ||
        descriptor->kind != RIBON_PLUGIN_KIND_IMAGE_FORMAT ||
        descriptor->operations_size != sizeof(struct RibonImageFormatOps) ||
        descriptor->operations_abi != RIBON_IMAGE_FORMAT_OPS_ABI_VERSION ||
        descriptor->provides != RIBON_CAP_IMAGE_ELF64) {
        return 0;
    }
    ops = (const struct RibonImageFormatOps *)descriptor->operations;
    return ribon_image_format_ops_are_valid(ops) &&
           ops->format == RIBON_EXECUTABLE_FORMAT_ELF64;
}

/** @brief ELF64 image-format plugin descriptor다. */
const struct RibonPluginDescriptor ribon_elf64_image_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_elf64_image_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_IMAGE_FORMAT,
    .phase = RIBON_PLUGIN_PHASE_BOOT,
    .id = "image.elf64",
    .provides = RIBON_CAP_IMAGE_ELF64,
    .requires = RIBON_CAP_ARCHITECTURE,
    .architecture_mask = RIBON_ARCH_MASK_ALL,
    .environment_mask = RIBON_ENV_MASK_ALL,
    .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 4096u,
    .input_budget = 64ull * 1024ull * 1024ull,
    .output_budget = 4096u,
    .deadline_ms = 30000u,
    .operations = &elf64_ops,
    .operations_size = sizeof(elf64_ops),
    .operations_abi = RIBON_IMAGE_FORMAT_OPS_ABI_VERSION,
    .validate_operations = ribon_image_plugin_operations_are_valid,
};
