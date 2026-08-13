#include <Ribon/boot/plan.h>
#include <Ribon/core/memory.h>
#include <Ribon/protocols/os/luca/rlh1.h>

#include <stddef.h>
#include <string.h>

#define RLH1_BASE_SECTION_CAPACITY 10u

struct Rlh1Writer {
    unsigned char *bytes;
    uint64_t capacity;
    uint64_t cursor;
    uint16_t section_capacity;
    uint16_t section_count;
};

/** @brief Little-endian 16-bit field를 기록한다. */
static void rlh1_write_u16(void *destination, uint64_t offset, uint16_t value) {
    volatile unsigned char *bytes =
        (volatile unsigned char *)destination + offset;
    bytes[0] = (unsigned char)(value & 0xffu);
    bytes[1] = (unsigned char)((value >> 8u) & 0xffu);
}

/** @brief Little-endian 32-bit field를 기록한다. */
static void rlh1_write_u32(void *destination, uint64_t offset, uint32_t value) {
    volatile unsigned char *bytes =
        (volatile unsigned char *)destination + offset;
    bytes[0] = (unsigned char)(value & 0xffu);
    bytes[1] = (unsigned char)((value >> 8u) & 0xffu);
    bytes[2] = (unsigned char)((value >> 16u) & 0xffu);
    bytes[3] = (unsigned char)((value >> 24u) & 0xffu);
}

/** @brief Little-endian 64-bit field를 기록한다. */
static void rlh1_write_u64(void *destination, uint64_t offset, uint64_t value) {
    volatile unsigned char *bytes =
        (volatile unsigned char *)destination + offset;
    for (uint32_t index = 0; index < 8u; ++index) {
        bytes[index] = (unsigned char)((value >> (index * 8u)) & 0xffu);
    }
}

/** @brief RLH1 wire domain separator를 header에 기록한다. */
static void rlh1_write_domain(void *destination) {
    static const unsigned char domain[RIBON_LUCA_RLH1_DOMAIN_SIZE] = {
        'R', 'I', 'B', 'O', 'N', '_', 'L', 'U',
        'C', 'A', '_', 'R', 'L', 'H', '1', '\0',
    };
    unsigned char *bytes = (unsigned char *)destination;
    for (uint32_t index = 0u; index < RIBON_LUCA_RLH1_DOMAIN_SIZE; ++index) {
        bytes[RIBON_LUCA_RLH1_HEADER_DOMAIN_OFFSET + index] = domain[index];
    }
}

/** @brief 두 unsigned 64-bit 값의 덧셈 overflow 여부를 반환한다. */
static int rlh1_add_overflows(uint64_t lhs, uint64_t rhs) {
    return lhs > UINT64_MAX - rhs;
}

static int rlh1_reserve_section(
    struct Rlh1Writer *writer,
    uint32_t type,
    uint32_t flags,
    uint64_t payload_size,
    unsigned char **payload_out) {
    uint64_t payload_offset;
    unsigned char *section;
    if (writer == 0 || writer->bytes == 0 || payload_out == 0 ||
        writer->section_count >= writer->section_capacity ||
        writer->section_capacity > RIBON_LUCA_RLH1_MAX_SECTIONS) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_BAD_ARGUMENT;
    }
    if (ribon_align_up(
            writer->cursor,
            RIBON_LUCA_RLH1_PAYLOAD_ALIGNMENT,
            &payload_offset) != RIBON_MEMORY_STATUS_OK ||
        payload_offset > writer->capacity || payload_size > writer->capacity - payload_offset) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_OUT_OF_CAPACITY;
    }
    section = writer->bytes + RIBON_LUCA_RLH1_HEADER_SIZE +
              ((uint64_t)writer->section_count * RIBON_LUCA_RLH1_SECTION_ENTRY_SIZE);
    rlh1_write_u32(section, RIBON_LUCA_RLH1_SECTION_TYPE_OFFSET, type);
    rlh1_write_u32(section, RIBON_LUCA_RLH1_SECTION_FLAGS_OFFSET, flags);
    rlh1_write_u64(section, RIBON_LUCA_RLH1_SECTION_PAYLOAD_OFFSET, payload_offset);
    rlh1_write_u64(section, RIBON_LUCA_RLH1_SECTION_LENGTH_OFFSET, payload_size);
    rlh1_write_u32(
        section,
        RIBON_LUCA_RLH1_SECTION_ALIGNMENT_OFFSET,
        RIBON_LUCA_RLH1_PAYLOAD_ALIGNMENT);
    rlh1_write_u32(section, RIBON_LUCA_RLH1_SECTION_RESERVED_OFFSET, 0u);
    *payload_out = writer->bytes + payload_offset;
    writer->cursor = payload_offset + payload_size;
    ++writer->section_count;
    return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
}

/** @brief Core memory kind를 RLH1 wire value로 변환한다. */
static uint32_t rlh1_memory_kind(enum RibonMemoryRegionKind kind) {
    switch (kind) {
    case RIBON_MEMORY_REGION_USABLE:
        return 1u;
    case RIBON_MEMORY_REGION_RESERVED:
        return 2u;
    case RIBON_MEMORY_REGION_ACPI:
        return 3u;
    case RIBON_MEMORY_REGION_MMIO:
        return 4u;
    case RIBON_MEMORY_REGION_FRAMEBUFFER:
        return 5u;
    case RIBON_MEMORY_REGION_FIRMWARE:
        return 6u;
    case RIBON_MEMORY_REGION_BOOTLOADER:
        return 7u;
    case RIBON_MEMORY_REGION_KERNEL_IMAGE:
        return 8u;
    case RIBON_MEMORY_REGION_BOOT_MODULE:
        return 9u;
    case RIBON_MEMORY_REGION_UNKNOWN:
    default:
        return 0u;
    }
}

static int rlh1_append_region_list(
    struct Rlh1Writer *writer,
    uint32_t section_type,
    const struct RibonMutableMemoryMap *memory_map,
    int reserved_only) {
    uint32_t encoded_count = 0;
    uint64_t payload_size;
    unsigned char *payload;
    int status;
    for (uint32_t index = 0; index < memory_map->region_count; ++index) {
        if (!reserved_only || memory_map->regions[index].kind != RIBON_MEMORY_REGION_USABLE) {
            ++encoded_count;
        }
    }
    payload_size = 8u + ((uint64_t)encoded_count * RIBON_LUCA_RLH1_REGION_ENTRY_SIZE);
    status = rlh1_reserve_section(
        writer,
        section_type,
        RIBON_LUCA_RLH1_SECTION_REQUIRED_TO_UNDERSTAND,
        payload_size,
        &payload);
    if (status != RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        return status;
    }
    rlh1_write_u32(payload, 0u, encoded_count);
    rlh1_write_u32(payload, 4u, RIBON_LUCA_RLH1_REGION_ENTRY_SIZE);
    encoded_count = 0;
    for (uint32_t index = 0; index < memory_map->region_count; ++index) {
        const struct RibonMemoryRegion *region = &memory_map->regions[index];
        unsigned char *entry;
        if (reserved_only && region->kind == RIBON_MEMORY_REGION_USABLE) {
            continue;
        }
        entry = payload + 8u + ((uint64_t)encoded_count * RIBON_LUCA_RLH1_REGION_ENTRY_SIZE);
        rlh1_write_u64(entry, 0u, region->base);
        rlh1_write_u64(entry, 8u, region->length);
        rlh1_write_u32(entry, 16u, rlh1_memory_kind(region->kind));
        rlh1_write_u32(entry, 20u, 0u);
        rlh1_write_u64(entry, 24u, region->attributes);
        ++encoded_count;
    }
    return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
}

static int rlh1_append_kernel_layout(
    struct Rlh1Writer *writer,
    const struct RibonBootPlan *plan) {
    uint32_t segment_count = plan->kernel_load_segment_count;
    uint64_t payload_size;
    unsigned char *payload;
    int status;
    if (segment_count > RIBON_LUCA_RLH1_MAX_KERNEL_SEGMENTS ||
        (segment_count != 0u && plan->kernel_load_segments == 0)) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN;
    }
    payload_size = RIBON_LUCA_RLH1_KERNEL_LAYOUT_SIZE +
                   ((uint64_t)segment_count * RIBON_LUCA_RLH1_KERNEL_SEGMENT_SIZE);
    status = rlh1_reserve_section(
        writer,
        RIBON_LUCA_RLH1_SECTION_KERNEL_LAYOUT,
        RIBON_LUCA_RLH1_SECTION_REQUIRED_TO_UNDERSTAND,
        payload_size,
        &payload);
    if (status != RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        return status;
    }
    rlh1_write_u32(payload, 0u, 1u);
    rlh1_write_u32(payload, 4u, segment_count);
    rlh1_write_u64(payload, 8u, plan->kernel_entry_point);
    rlh1_write_u64(payload, 16u, plan->kernel_entry_load_address);
    rlh1_write_u64(payload, 24u, plan->kernel_runtime_entry_address);
    rlh1_write_u64(payload, 32u, plan->kernel_load_base);
    rlh1_write_u64(payload, 40u, plan->kernel_load_end);
    rlh1_write_u64(payload, 48u, plan->kernel_runtime_load_base);
    rlh1_write_u64(payload, 56u, plan->kernel_runtime_load_end);
    rlh1_write_u64(payload, 64u, plan->kernel_linked_virtual_base);
    rlh1_write_u64(payload, 72u, plan->kernel_linked_virtual_end);
    rlh1_write_u64(payload, 80u, plan->kernel_linked_physical_base);
    rlh1_write_u64(payload, 88u, plan->kernel_linked_physical_end);
    rlh1_write_u64(payload, 96u, plan->kernel_high_entry_virtual_address);
    rlh1_write_u64(payload, 104u, plan->kernel_high_entry_load_address);
    rlh1_write_u64(payload, 112u, plan->kernel_memory_size);
    rlh1_write_u32(payload, 120u, plan->kernel_load_plan_flags);
    rlh1_write_u32(payload, 124u, 0u);
    for (uint32_t index = 0; index < segment_count; ++index) {
        const struct RibonLoadSegment *source = &plan->kernel_load_segments[index];
        unsigned char *entry = payload + RIBON_LUCA_RLH1_KERNEL_LAYOUT_SIZE +
                               ((uint64_t)index * RIBON_LUCA_RLH1_KERNEL_SEGMENT_SIZE);
        rlh1_write_u64(entry, 0u, source->virtual_address);
        rlh1_write_u64(entry, 8u, source->physical_address);
        rlh1_write_u64(entry, 16u, source->load_address);
        rlh1_write_u64(entry, 24u, source->runtime_address);
        rlh1_write_u64(entry, 32u, source->memory_size);
        rlh1_write_u64(entry, 40u, source->file_size);
        rlh1_write_u64(entry, 48u, source->alignment);
        rlh1_write_u64(entry, 56u, source->flags);
    }
    return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
}

/** @brief Architecture descriptor를 RLH1 provenance ID로 변환한다. */
static uint32_t rlh1_arch_id(const struct RibonArchDescriptor *arch) {
    if (arch == 0) {
        return 0u;
    }
    switch (arch->id) {
    case RIBON_ARCHITECTURE_X86_64:
        return 1u;
    case RIBON_ARCHITECTURE_AARCH64:
        return 2u;
    case RIBON_ARCHITECTURE_RISCV64:
        return 3u;
    default:
        return 0u;
    }
}

static int rlh1_append_provenance(
    struct Rlh1Writer *writer,
    const struct RibonBootPlan *plan) {
    unsigned char *payload;
    int status = rlh1_reserve_section(
        writer,
        RIBON_LUCA_RLH1_SECTION_PROVENANCE,
        RIBON_LUCA_RLH1_SECTION_REQUIRED_TO_UNDERSTAND,
        RIBON_LUCA_RLH1_PROVENANCE_SIZE,
        &payload);
    if (status != RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        return status;
    }
    rlh1_write_u32(payload, 0u, (uint32_t)plan->environment);
    rlh1_write_u32(payload, 4u, rlh1_arch_id(plan->arch));
    rlh1_write_u32(payload, 8u, RIBON_VERSION_MAJOR);
    rlh1_write_u32(payload, 12u, RIBON_VERSION_MINOR);
    rlh1_write_u32(payload, 16u, RIBON_VERSION_PATCH);
    rlh1_write_u32(payload, 20u, 0u);
    rlh1_write_u64(payload, 24u, 0u);
    return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
}

/**
 * @brief RISC-V primary entry의 bootstrap hart identity를 RLH1에 기록한다.
 *
 * 이 section은 artifact 안에 복사되는 fixed payload이며 borrowed native pointer를
 * 포함하지 않는다.
 */
static int rlh1_append_boot_cpu(
    struct Rlh1Writer *writer,
    const struct RibonBootPlan *plan,
    const struct RibonBootEnvironment *environment) {
    unsigned char *payload;
    int status;
    if (plan->arch == 0 ||
        plan->arch->id != RIBON_ARCHITECTURE_RISCV64) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
    }
    if (environment->architecture != RIBON_ARCHITECTURE_RISCV64 ||
        (environment->flags & RIBON_BOOT_ENV_HAS_BOOT_CPU_ID) == 0u) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN;
    }
    if (environment->kind == RIBON_ENVIRONMENT_RAW_FDT &&
        ((environment->flags & RIBON_BOOT_ENV_HAS_DEVICE_TREE) == 0u ||
         environment->device_tree.physical_address == 0u ||
         environment->device_tree.size == 0u)) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN;
    }
    status = rlh1_reserve_section(
        writer,
        RIBON_LUCA_RLH1_SECTION_BOOT_CPU,
        RIBON_LUCA_RLH1_SECTION_REQUIRED_TO_UNDERSTAND,
        RIBON_LUCA_RLH1_BOOT_CPU_SIZE,
        &payload);
    if (status != RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        return status;
    }
    rlh1_write_u64(
        payload,
        RIBON_LUCA_RLH1_BOOT_CPU_ID_OFFSET,
        environment->boot_cpu_id);
    rlh1_write_u32(
        payload,
        RIBON_LUCA_RLH1_BOOT_CPU_NAMESPACE_OFFSET,
        RIBON_LUCA_RLH1_BOOT_CPU_NAMESPACE_RISCV_HART_ID);
    rlh1_write_u32(
        payload,
        RIBON_LUCA_RLH1_BOOT_CPU_FLAGS_OFFSET,
        RIBON_LUCA_RLH1_BOOT_CPU_FLAG_BOOTSTRAP);
    rlh1_write_u64(
        payload,
        RIBON_LUCA_RLH1_BOOT_CPU_RESERVED0_OFFSET,
        0u);
    rlh1_write_u64(
        payload,
        RIBON_LUCA_RLH1_BOOT_CPU_RESERVED1_OFFSET,
        0u);
    return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
}

static int rlh1_append_range_descriptor(
    struct Rlh1Writer *writer,
    uint32_t type,
    uint64_t physical_address,
    uint64_t size,
    uint32_t metadata0,
    uint32_t metadata1) {
    unsigned char *payload;
    int status;
    if (physical_address == 0u || size == 0u) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
    }
    status = rlh1_reserve_section(
        writer,
        type,
        RIBON_LUCA_RLH1_SECTION_BORROWED_RANGE_DESCRIPTOR,
        RIBON_LUCA_RLH1_RANGE_DESCRIPTOR_SIZE,
        &payload);
    if (status != RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        return status;
    }
    rlh1_write_u64(payload, 0u, physical_address);
    rlh1_write_u64(payload, 8u, size);
    rlh1_write_u32(payload, 16u, metadata0);
    rlh1_write_u32(payload, 20u, metadata1);
    rlh1_write_u64(payload, 24u, 0u);
    return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
}

static int rlh1_append_command_line(
    struct Rlh1Writer *writer,
    const struct RibonBootEnvironment *environment) {
    unsigned char *payload;
    uint64_t length;
    int status;
    if ((environment->flags & RIBON_BOOT_ENV_HAS_COMMAND_LINE) == 0u ||
        environment->command_line.text == 0) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
    }
    length = environment->command_line.length;
    if (length == 0u) {
        length = strlen(environment->command_line.text);
    }
    if (length >= RIBON_LUCA_RLH1_COMMAND_LINE_MAX_SIZE) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_OUT_OF_CAPACITY;
    }
    status = rlh1_reserve_section(
        writer,
        RIBON_LUCA_RLH1_SECTION_COMMAND_LINE,
        0u,
        length + 1u,
        &payload);
    if (status != RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        return status;
    }
    memcpy(payload, environment->command_line.text, (size_t)length);
    payload[length] = '\0';
    return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
}

static int rlh1_append_framebuffer(
    struct Rlh1Writer *writer,
    const struct RibonBootEnvironment *environment) {
    unsigned char *payload;
    int status;
    if ((environment->flags & RIBON_BOOT_ENV_HAS_FRAMEBUFFER) == 0u ||
        environment->framebuffer.physical_address == 0u) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
    }
    status = rlh1_reserve_section(
        writer,
        RIBON_LUCA_RLH1_SECTION_FRAMEBUFFER,
        0u,
        RIBON_LUCA_RLH1_FRAMEBUFFER_SIZE,
        &payload);
    if (status != RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        return status;
    }
    rlh1_write_u64(payload, 0u, environment->framebuffer.physical_address);
    rlh1_write_u32(payload, 8u, environment->framebuffer.width);
    rlh1_write_u32(payload, 12u, environment->framebuffer.height);
    rlh1_write_u32(payload, 16u, environment->framebuffer.pitch);
    rlh1_write_u32(payload, 20u, environment->framebuffer.bits_per_pixel);
    rlh1_write_u32(payload, 24u, (uint32_t)environment->framebuffer.backend);
    payload[28] = environment->framebuffer.rgb.red_position;
    payload[29] = environment->framebuffer.rgb.red_mask_size;
    payload[30] = environment->framebuffer.rgb.green_position;
    payload[31] = environment->framebuffer.rgb.green_mask_size;
    payload[32] = environment->framebuffer.rgb.blue_position;
    payload[33] = environment->framebuffer.rgb.blue_mask_size;
    return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
}

static int rlh1_append_modules(
    struct Rlh1Writer *writer,
    const struct RibonBootPlan *plan,
    const struct RibonBootEnvironment *environment) {
    uint32_t count = environment->boot_modules.module_count;
    uint64_t payload_size;
    unsigned char *payload;
    int status;
    if ((environment->flags & RIBON_BOOT_ENV_HAS_BOOT_MODULES) == 0u || count == 0u) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
    }
    if (environment->boot_modules.modules == 0 ||
        count > RIBON_LUCA_RLH1_MAX_MODULES ||
        plan->kernel_load_end <= plan->kernel_load_base) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN;
    }
    for (uint32_t index = 0u; index < count; ++index) {
        const struct RibonBootModule *module =
            &environment->boot_modules.modules[index];
        uint64_t module_end;
        if (module->physical_address == 0u || module->size == 0u ||
            rlh1_add_overflows(module->physical_address, module->size) ||
            (module->role != RIBON_BOOT_MODULE_ROLE_INITIAL_IMAGE &&
             module->role != RIBON_BOOT_MODULE_ROLE_AUXILIARY)) {
            return RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN;
        }
        module_end = module->physical_address + module->size;
        if (module->physical_address < plan->kernel_load_end &&
            plan->kernel_load_base < module_end) {
            return RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN;
        }
        for (uint32_t previous = 0u; previous < index; ++previous) {
            const struct RibonBootModule *other =
                &environment->boot_modules.modules[previous];
            if (module->physical_address <
                    other->physical_address + other->size &&
                other->physical_address < module_end) {
                return RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN;
            }
        }
    }
    payload_size = 8u + ((uint64_t)count * RIBON_LUCA_RLH1_MODULE_ENTRY_SIZE);
    status = rlh1_reserve_section(
        writer,
        RIBON_LUCA_RLH1_SECTION_MODULES,
        RIBON_LUCA_RLH1_SECTION_REQUIRED_TO_UNDERSTAND |
            RIBON_LUCA_RLH1_SECTION_BORROWED_RANGE_DESCRIPTOR,
        payload_size,
        &payload);
    if (status != RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        return status;
    }
    rlh1_write_u32(payload, 0u, count);
    rlh1_write_u32(payload, 4u, RIBON_LUCA_RLH1_MODULE_ENTRY_SIZE);
    for (uint32_t index = 0; index < count; ++index) {
        const struct RibonBootModule *source = &environment->boot_modules.modules[index];
        unsigned char *entry = payload + 8u +
                               ((uint64_t)index * RIBON_LUCA_RLH1_MODULE_ENTRY_SIZE);
        rlh1_write_u64(entry, 0u, source->physical_address);
        rlh1_write_u64(entry, 8u, source->size);
        rlh1_write_u32(
            entry,
            16u,
            source->role == RIBON_BOOT_MODULE_ROLE_INITIAL_IMAGE ?
                RIBON_LUCA_RLH1_MODULE_FLAG_INITIAL_IMAGE :
                0u);
        rlh1_write_u32(entry, 20u, 0u);
        rlh1_write_u64(entry, 24u, 0u);
    }
    return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
}

static int rlh1_append_boot_media(
    struct Rlh1Writer *writer,
    const struct RibonBootEnvironment *environment) {
    unsigned char *payload;
    int status;
    if ((environment->flags & RIBON_BOOT_ENV_HAS_BOOT_MEDIA) == 0u) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
    }
    status = rlh1_reserve_section(
        writer,
        RIBON_LUCA_RLH1_SECTION_BOOT_MEDIA,
        0u,
        RIBON_LUCA_RLH1_BOOT_MEDIA_SIZE,
        &payload);
    if (status != RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        return status;
    }
    rlh1_write_u32(payload, 0u, (uint32_t)environment->boot_media.kind);
    rlh1_write_u32(payload, 4u, environment->boot_media.block_size);
    rlh1_write_u64(payload, 8u, environment->boot_media.physical_address);
    rlh1_write_u64(payload, 16u, environment->boot_media.size);
    rlh1_write_u64(payload, 24u, 0u);
    return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
}

/** @brief CRC field를 0으로 간주해 RLH1 CRC32C를 계산한다. */
uint32_t ribon_luca_rlh1_crc32c(const void *data, uint64_t size) {
    const unsigned char *bytes = (const unsigned char *)data;
    uint32_t crc = UINT32_MAX;
    if (bytes == 0) {
        return 0u;
    }
    for (uint64_t index = 0; index < size; ++index) {
        unsigned char value = bytes[index];
        if (index >= RIBON_LUCA_RLH1_HEADER_CRC32C_OFFSET &&
            index < RIBON_LUCA_RLH1_HEADER_CRC32C_OFFSET + 4u) {
            value = 0u;
        }
        crc ^= value;
        for (uint32_t bit = 0; bit < 8u; ++bit) {
            crc = (crc >> 1u) ^ (0x82f63b78u & (uint32_t)-(int32_t)(crc & 1u));
        }
    }
    return ~crc;
}

int ribon_luca_build_rlh1(
    const struct RibonBootPlan *plan,
    const struct RibonBootEnvironment *environment,
    const struct RibonMutableMemoryMap *normalized_memory_map,
    void *buffer,
    uint64_t capacity,
    struct RibonHandoffArtifact *out) {
    struct Rlh1Writer writer;
    uint64_t table_capacity;
    uint16_t section_capacity;
    uint32_t total_size;
    uint32_t checksum;
    struct RibonLucaRlh1View validated_view;
    int status;
    if (plan == 0 || environment == 0 || normalized_memory_map == 0 ||
        normalized_memory_map->regions == 0 || normalized_memory_map->region_count == 0u ||
        buffer == 0 || out == 0 || capacity < RIBON_LUCA_RLH1_HEADER_SIZE ||
        capacity > UINT32_MAX) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_BAD_ARGUMENT;
    }
    if (capacity > RIBON_LUCA_RLH1_MAX_TOTAL_SIZE) {
        capacity = RIBON_LUCA_RLH1_MAX_TOTAL_SIZE;
    }
    section_capacity =
        (uint16_t)(RLH1_BASE_SECTION_CAPACITY +
                   (plan->arch != 0 &&
                            plan->arch->id == RIBON_ARCHITECTURE_RISCV64 ?
                        1u :
                        0u));
    table_capacity = (uint64_t)section_capacity *
                     RIBON_LUCA_RLH1_SECTION_ENTRY_SIZE;
    if (rlh1_add_overflows(RIBON_LUCA_RLH1_HEADER_SIZE, table_capacity)) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_OUT_OF_CAPACITY;
    }
    memset(buffer, 0, (size_t)capacity);
    writer.bytes = (unsigned char *)buffer;
    writer.capacity = capacity;
    writer.cursor = RIBON_LUCA_RLH1_HEADER_SIZE + table_capacity;
    writer.section_capacity = section_capacity;
    writer.section_count = 0u;

    status = rlh1_append_region_list(
        &writer,
        RIBON_LUCA_RLH1_SECTION_MEMORY_MAP,
        normalized_memory_map,
        0);
    if (status == RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        status = rlh1_append_region_list(
            &writer,
            RIBON_LUCA_RLH1_SECTION_RESERVED_RANGES,
            normalized_memory_map,
            1);
    }
    if (status == RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        status = rlh1_append_kernel_layout(&writer, plan);
    }
    if (status == RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        status = rlh1_append_range_descriptor(
            &writer,
            RIBON_LUCA_RLH1_SECTION_DEVICE_TREE,
            environment->device_tree.physical_address,
            environment->device_tree.size,
            0u,
            0u);
    }
    if (status == RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        status = rlh1_append_range_descriptor(
            &writer,
            RIBON_LUCA_RLH1_SECTION_ACPI,
            environment->acpi_rsdp.physical_address,
            environment->acpi_rsdp.size,
            environment->acpi_rsdp.revision,
            0u);
    }
    if (status == RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        status = rlh1_append_command_line(&writer, environment);
    }
    if (status == RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        status = rlh1_append_framebuffer(&writer, environment);
    }
    if (status == RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        status = rlh1_append_modules(&writer, plan, environment);
    }
    if (status == RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        status = rlh1_append_boot_media(&writer, environment);
    }
    if (status == RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        status = rlh1_append_provenance(&writer, plan);
    }
    if (status == RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        status = rlh1_append_boot_cpu(&writer, plan, environment);
    }
    if (status != RIBON_PROTOCOL_HANDOFF_STATUS_OK || writer.cursor > UINT32_MAX) {
        return status != RIBON_PROTOCOL_HANDOFF_STATUS_OK ?
                   status :
                   RIBON_PROTOCOL_HANDOFF_STATUS_OUT_OF_CAPACITY;
    }

    total_size = (uint32_t)writer.cursor;
    rlh1_write_u32(buffer, RIBON_LUCA_RLH1_HEADER_MAGIC_OFFSET, RIBON_LUCA_RLH1_MAGIC);
    rlh1_write_u16(
        buffer,
        RIBON_LUCA_RLH1_HEADER_VERSION_MAJOR_OFFSET,
        RIBON_LUCA_RLH1_VERSION_MAJOR);
    rlh1_write_u16(
        buffer,
        RIBON_LUCA_RLH1_HEADER_VERSION_MINOR_OFFSET,
        RIBON_LUCA_RLH1_VERSION_MINOR);
    rlh1_write_u16(buffer, RIBON_LUCA_RLH1_HEADER_SIZE_OFFSET, RIBON_LUCA_RLH1_HEADER_SIZE);
    rlh1_write_u16(
        buffer,
        RIBON_LUCA_RLH1_HEADER_SECTION_ENTRY_SIZE_OFFSET,
        RIBON_LUCA_RLH1_SECTION_ENTRY_SIZE);
    rlh1_write_u16(
        buffer,
        RIBON_LUCA_RLH1_HEADER_SECTION_COUNT_OFFSET,
        writer.section_count);
    rlh1_write_u32(buffer, RIBON_LUCA_RLH1_HEADER_TOTAL_SIZE_OFFSET, total_size);
    rlh1_write_u32(
        buffer,
        RIBON_LUCA_RLH1_HEADER_SECTION_TABLE_OFFSET,
        RIBON_LUCA_RLH1_HEADER_SIZE);
    rlh1_write_u64(buffer, RIBON_LUCA_RLH1_HEADER_FLAGS_OFFSET, RIBON_LUCA_RLH1_FLAG_NONE);
    rlh1_write_domain(buffer);
    rlh1_write_u64(buffer, RIBON_LUCA_RLH1_HEADER_RESERVED2_OFFSET, 0u);
    checksum = ribon_luca_rlh1_crc32c(buffer, total_size);
    rlh1_write_u32(buffer, RIBON_LUCA_RLH1_HEADER_CRC32C_OFFSET, checksum);
    if (ribon_luca_parse_rlh1(buffer, total_size, &validated_view) !=
            RIBON_LUCA_RLH1_PARSE_OK ||
        validated_view.total_size != total_size ||
        validated_view.section_count != writer.section_count) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN;
    }

    out->data = buffer;
    out->size = total_size;
    out->format = "rlh1";
    out->version_major = RIBON_LUCA_RLH1_VERSION_MAJOR;
    out->section_count = writer.section_count;
    return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
}
