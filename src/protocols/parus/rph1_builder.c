#include <Ribon/boot/plan.h>
#include <Ribon/core/memory.h>
#include <Ribon/protocols/parus/rph1.h>

#include <stddef.h>
#include <string.h>

#define RPH1_IMPLEMENTED_SECTION_COUNT 10u

struct Rph1Writer {
    unsigned char *bytes;
    uint64_t capacity;
    uint64_t cursor;
    uint16_t section_count;
};

/** @brief Little-endian 16-bit field를 기록한다. */
static void rph1_write_u16(void *destination, uint64_t offset, uint16_t value) {
    volatile unsigned char *bytes =
        (volatile unsigned char *)destination + offset;
    bytes[0] = (unsigned char)(value & 0xffu);
    bytes[1] = (unsigned char)((value >> 8u) & 0xffu);
}

/** @brief Little-endian 32-bit field를 기록한다. */
static void rph1_write_u32(void *destination, uint64_t offset, uint32_t value) {
    volatile unsigned char *bytes =
        (volatile unsigned char *)destination + offset;
    bytes[0] = (unsigned char)(value & 0xffu);
    bytes[1] = (unsigned char)((value >> 8u) & 0xffu);
    bytes[2] = (unsigned char)((value >> 16u) & 0xffu);
    bytes[3] = (unsigned char)((value >> 24u) & 0xffu);
}

/** @brief Little-endian 64-bit field를 기록한다. */
static void rph1_write_u64(void *destination, uint64_t offset, uint64_t value) {
    volatile unsigned char *bytes =
        (volatile unsigned char *)destination + offset;
    for (uint32_t index = 0; index < 8u; ++index) {
        bytes[index] = (unsigned char)((value >> (index * 8u)) & 0xffu);
    }
}

/** @brief 두 unsigned 64-bit 값의 덧셈 overflow 여부를 반환한다. */
static int rph1_add_overflows(uint64_t lhs, uint64_t rhs) {
    return lhs > UINT64_MAX - rhs;
}

static int rph1_reserve_section(
    struct Rph1Writer *writer,
    uint32_t type,
    uint32_t flags,
    uint64_t payload_size,
    unsigned char **payload_out) {
    uint64_t payload_offset;
    unsigned char *section;
    if (writer == 0 || writer->bytes == 0 || payload_out == 0 ||
        writer->section_count >= RIBON_PARUS_RPH1_MAX_SECTIONS) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_BAD_ARGUMENT;
    }
    if (ribon_align_up(
            writer->cursor,
            RIBON_PARUS_RPH1_PAYLOAD_ALIGNMENT,
            &payload_offset) != RIBON_MEMORY_STATUS_OK ||
        payload_offset > writer->capacity || payload_size > writer->capacity - payload_offset) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_OUT_OF_CAPACITY;
    }
    section = writer->bytes + RIBON_PARUS_RPH1_HEADER_SIZE +
              ((uint64_t)writer->section_count * RIBON_PARUS_RPH1_SECTION_ENTRY_SIZE);
    rph1_write_u32(section, RIBON_PARUS_RPH1_SECTION_TYPE_OFFSET, type);
    rph1_write_u32(section, RIBON_PARUS_RPH1_SECTION_FLAGS_OFFSET, flags);
    rph1_write_u64(section, RIBON_PARUS_RPH1_SECTION_PAYLOAD_OFFSET, payload_offset);
    rph1_write_u64(section, RIBON_PARUS_RPH1_SECTION_LENGTH_OFFSET, payload_size);
    rph1_write_u32(
        section,
        RIBON_PARUS_RPH1_SECTION_ALIGNMENT_OFFSET,
        RIBON_PARUS_RPH1_PAYLOAD_ALIGNMENT);
    rph1_write_u32(section, RIBON_PARUS_RPH1_SECTION_RESERVED_OFFSET, 0u);
    *payload_out = writer->bytes + payload_offset;
    writer->cursor = payload_offset + payload_size;
    ++writer->section_count;
    return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
}

/** @brief Core memory kind를 RPH1 wire value로 변환한다. */
static uint32_t rph1_memory_kind(enum RibonMemoryRegionKind kind) {
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

static int rph1_append_region_list(
    struct Rph1Writer *writer,
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
    payload_size = 8u + ((uint64_t)encoded_count * RIBON_PARUS_RPH1_REGION_ENTRY_SIZE);
    status = rph1_reserve_section(
        writer,
        section_type,
        RIBON_PARUS_RPH1_SECTION_REQUIRED_TO_UNDERSTAND,
        payload_size,
        &payload);
    if (status != RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        return status;
    }
    rph1_write_u32(payload, 0u, encoded_count);
    rph1_write_u32(payload, 4u, RIBON_PARUS_RPH1_REGION_ENTRY_SIZE);
    encoded_count = 0;
    for (uint32_t index = 0; index < memory_map->region_count; ++index) {
        const struct RibonMemoryRegion *region = &memory_map->regions[index];
        unsigned char *entry;
        if (reserved_only && region->kind == RIBON_MEMORY_REGION_USABLE) {
            continue;
        }
        entry = payload + 8u + ((uint64_t)encoded_count * RIBON_PARUS_RPH1_REGION_ENTRY_SIZE);
        rph1_write_u64(entry, 0u, region->base);
        rph1_write_u64(entry, 8u, region->length);
        rph1_write_u32(entry, 16u, rph1_memory_kind(region->kind));
        rph1_write_u32(entry, 20u, 0u);
        rph1_write_u64(entry, 24u, region->attributes);
        ++encoded_count;
    }
    return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
}

static int rph1_append_kernel_layout(
    struct Rph1Writer *writer,
    const struct RibonBootPlan *plan) {
    uint32_t segment_count = plan->kernel_load_segment_count;
    uint64_t payload_size;
    unsigned char *payload;
    int status;
    if (segment_count > RIBON_PARUS_RPH1_MAX_KERNEL_SEGMENTS ||
        (segment_count != 0u && plan->kernel_load_segments == 0)) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN;
    }
    payload_size = RIBON_PARUS_RPH1_KERNEL_LAYOUT_SIZE +
                   ((uint64_t)segment_count * RIBON_PARUS_RPH1_KERNEL_SEGMENT_SIZE);
    status = rph1_reserve_section(
        writer,
        RIBON_PARUS_RPH1_SECTION_KERNEL_LAYOUT,
        RIBON_PARUS_RPH1_SECTION_REQUIRED_TO_UNDERSTAND,
        payload_size,
        &payload);
    if (status != RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        return status;
    }
    rph1_write_u32(payload, 0u, 1u);
    rph1_write_u32(payload, 4u, segment_count);
    rph1_write_u64(payload, 8u, plan->kernel_entry_point);
    rph1_write_u64(payload, 16u, plan->kernel_entry_load_address);
    rph1_write_u64(payload, 24u, plan->kernel_runtime_entry_address);
    rph1_write_u64(payload, 32u, plan->kernel_load_base);
    rph1_write_u64(payload, 40u, plan->kernel_load_end);
    rph1_write_u64(payload, 48u, plan->kernel_runtime_load_base);
    rph1_write_u64(payload, 56u, plan->kernel_runtime_load_end);
    rph1_write_u64(payload, 64u, plan->kernel_linked_virtual_base);
    rph1_write_u64(payload, 72u, plan->kernel_linked_virtual_end);
    rph1_write_u64(payload, 80u, plan->kernel_linked_physical_base);
    rph1_write_u64(payload, 88u, plan->kernel_linked_physical_end);
    rph1_write_u64(payload, 96u, plan->kernel_high_entry_virtual_address);
    rph1_write_u64(payload, 104u, plan->kernel_high_entry_load_address);
    rph1_write_u64(payload, 112u, plan->kernel_memory_size);
    rph1_write_u32(payload, 120u, plan->kernel_load_plan_flags);
    rph1_write_u32(payload, 124u, 0u);
    for (uint32_t index = 0; index < segment_count; ++index) {
        const struct RibonLoadSegment *source = &plan->kernel_load_segments[index];
        unsigned char *entry = payload + RIBON_PARUS_RPH1_KERNEL_LAYOUT_SIZE +
                               ((uint64_t)index * RIBON_PARUS_RPH1_KERNEL_SEGMENT_SIZE);
        rph1_write_u64(entry, 0u, source->virtual_address);
        rph1_write_u64(entry, 8u, source->physical_address);
        rph1_write_u64(entry, 16u, source->load_address);
        rph1_write_u64(entry, 24u, source->runtime_address);
        rph1_write_u64(entry, 32u, source->memory_size);
        rph1_write_u64(entry, 40u, source->file_size);
        rph1_write_u64(entry, 48u, source->alignment);
        rph1_write_u64(entry, 56u, source->flags);
    }
    return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
}

/** @brief Architecture descriptor를 RPH1 provenance ID로 변환한다. */
static uint32_t rph1_arch_id(const struct RibonArchDescriptor *arch) {
    if (arch == 0 || arch->canonical_name == 0) {
        return 0u;
    }
    if (strcmp(arch->canonical_name, "x86_64") == 0) {
        return 1u;
    }
    if (strcmp(arch->canonical_name, "aarch64") == 0) {
        return 2u;
    }
    if (strcmp(arch->canonical_name, "riscv64") == 0) {
        return 3u;
    }
    return 0u;
}

static int rph1_append_provenance(
    struct Rph1Writer *writer,
    const struct RibonBootPlan *plan) {
    unsigned char *payload;
    int status = rph1_reserve_section(
        writer,
        RIBON_PARUS_RPH1_SECTION_PROVENANCE,
        RIBON_PARUS_RPH1_SECTION_REQUIRED_TO_UNDERSTAND,
        RIBON_PARUS_RPH1_PROVENANCE_SIZE,
        &payload);
    if (status != RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        return status;
    }
    rph1_write_u32(payload, 0u, (uint32_t)plan->environment);
    rph1_write_u32(payload, 4u, rph1_arch_id(plan->arch));
    rph1_write_u32(payload, 8u, RIBON_VERSION_MAJOR);
    rph1_write_u32(payload, 12u, RIBON_VERSION_MINOR);
    rph1_write_u32(payload, 16u, RIBON_VERSION_PATCH);
    rph1_write_u32(payload, 20u, 0u);
    rph1_write_u64(payload, 24u, 0u);
    return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
}

static int rph1_append_range_descriptor(
    struct Rph1Writer *writer,
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
    status = rph1_reserve_section(
        writer,
        type,
        RIBON_PARUS_RPH1_SECTION_BORROWED_RANGE_DESCRIPTOR,
        RIBON_PARUS_RPH1_RANGE_DESCRIPTOR_SIZE,
        &payload);
    if (status != RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        return status;
    }
    rph1_write_u64(payload, 0u, physical_address);
    rph1_write_u64(payload, 8u, size);
    rph1_write_u32(payload, 16u, metadata0);
    rph1_write_u32(payload, 20u, metadata1);
    rph1_write_u64(payload, 24u, 0u);
    return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
}

static int rph1_append_command_line(
    struct Rph1Writer *writer,
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
    if (length == UINT64_MAX) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_OUT_OF_CAPACITY;
    }
    status = rph1_reserve_section(
        writer,
        RIBON_PARUS_RPH1_SECTION_COMMAND_LINE,
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

static int rph1_append_framebuffer(
    struct Rph1Writer *writer,
    const struct RibonBootEnvironment *environment) {
    unsigned char *payload;
    int status;
    if ((environment->flags & RIBON_BOOT_ENV_HAS_FRAMEBUFFER) == 0u ||
        environment->framebuffer.physical_address == 0u) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
    }
    status = rph1_reserve_section(
        writer,
        RIBON_PARUS_RPH1_SECTION_FRAMEBUFFER,
        0u,
        RIBON_PARUS_RPH1_FRAMEBUFFER_SIZE,
        &payload);
    if (status != RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        return status;
    }
    rph1_write_u64(payload, 0u, environment->framebuffer.physical_address);
    rph1_write_u32(payload, 8u, environment->framebuffer.width);
    rph1_write_u32(payload, 12u, environment->framebuffer.height);
    rph1_write_u32(payload, 16u, environment->framebuffer.pitch);
    rph1_write_u32(payload, 20u, environment->framebuffer.bits_per_pixel);
    rph1_write_u32(payload, 24u, (uint32_t)environment->framebuffer.backend);
    payload[28] = environment->framebuffer.rgb.red_position;
    payload[29] = environment->framebuffer.rgb.red_mask_size;
    payload[30] = environment->framebuffer.rgb.green_position;
    payload[31] = environment->framebuffer.rgb.green_mask_size;
    payload[32] = environment->framebuffer.rgb.blue_position;
    payload[33] = environment->framebuffer.rgb.blue_mask_size;
    return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
}

static int rph1_append_modules(
    struct Rph1Writer *writer,
    const struct RibonBootEnvironment *environment) {
    uint32_t count = environment->boot_modules.module_count;
    uint64_t payload_size;
    unsigned char *payload;
    int status;
    if ((environment->flags & RIBON_BOOT_ENV_HAS_BOOT_MODULES) == 0u || count == 0u) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
    }
    if (environment->boot_modules.modules == 0 ||
        count > (RIBON_PARUS_RPH1_MAX_TOTAL_SIZE - 8u) / RIBON_PARUS_RPH1_MODULE_ENTRY_SIZE) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN;
    }
    payload_size = 8u + ((uint64_t)count * RIBON_PARUS_RPH1_MODULE_ENTRY_SIZE);
    status = rph1_reserve_section(
        writer,
        RIBON_PARUS_RPH1_SECTION_MODULES,
        0u,
        payload_size,
        &payload);
    if (status != RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        return status;
    }
    rph1_write_u32(payload, 0u, count);
    rph1_write_u32(payload, 4u, RIBON_PARUS_RPH1_MODULE_ENTRY_SIZE);
    for (uint32_t index = 0; index < count; ++index) {
        const struct RibonBootModule *source = &environment->boot_modules.modules[index];
        unsigned char *entry = payload + 8u +
                               ((uint64_t)index * RIBON_PARUS_RPH1_MODULE_ENTRY_SIZE);
        rph1_write_u64(entry, 0u, source->physical_address);
        rph1_write_u64(entry, 8u, source->size);
        rph1_write_u32(entry, 16u, source->flags);
        rph1_write_u32(entry, 20u, 0u);
        rph1_write_u64(entry, 24u, 0u);
    }
    return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
}

static int rph1_append_boot_media(
    struct Rph1Writer *writer,
    const struct RibonBootEnvironment *environment) {
    unsigned char *payload;
    int status;
    if ((environment->flags & RIBON_BOOT_ENV_HAS_BOOT_MEDIA) == 0u) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
    }
    status = rph1_reserve_section(
        writer,
        RIBON_PARUS_RPH1_SECTION_BOOT_MEDIA,
        0u,
        RIBON_PARUS_RPH1_BOOT_MEDIA_SIZE,
        &payload);
    if (status != RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        return status;
    }
    rph1_write_u32(payload, 0u, (uint32_t)environment->boot_media.kind);
    rph1_write_u32(payload, 4u, environment->boot_media.block_size);
    rph1_write_u64(payload, 8u, environment->boot_media.physical_address);
    rph1_write_u64(payload, 16u, environment->boot_media.size);
    rph1_write_u64(payload, 24u, 0u);
    return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
}

/** @brief CRC field를 0으로 간주해 RPH1 CRC32C를 계산한다. */
uint32_t ribon_parus_rph1_crc32c(const void *data, uint64_t size) {
    const unsigned char *bytes = (const unsigned char *)data;
    uint32_t crc = UINT32_MAX;
    if (bytes == 0) {
        return 0u;
    }
    for (uint64_t index = 0; index < size; ++index) {
        unsigned char value = bytes[index];
        if (index >= RIBON_PARUS_RPH1_HEADER_CRC32C_OFFSET &&
            index < RIBON_PARUS_RPH1_HEADER_CRC32C_OFFSET + 4u) {
            value = 0u;
        }
        crc ^= value;
        for (uint32_t bit = 0; bit < 8u; ++bit) {
            crc = (crc >> 1u) ^ (0x82f63b78u & (uint32_t)-(int32_t)(crc & 1u));
        }
    }
    return ~crc;
}

int ribon_parus_build_rph1(
    const struct RibonBootPlan *plan,
    const struct RibonBootEnvironment *environment,
    const struct RibonMutableMemoryMap *normalized_memory_map,
    void *buffer,
    uint64_t capacity,
    struct RibonHandoffArtifact *out) {
    struct Rph1Writer writer;
    uint64_t table_capacity;
    uint32_t total_size;
    uint32_t checksum;
    struct RibonParusRph1View validated_view;
    int status;
    if (plan == 0 || environment == 0 || normalized_memory_map == 0 ||
        normalized_memory_map->regions == 0 || normalized_memory_map->region_count == 0u ||
        buffer == 0 || out == 0 || capacity < RIBON_PARUS_RPH1_HEADER_SIZE ||
        capacity > UINT32_MAX) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_BAD_ARGUMENT;
    }
    if (capacity > RIBON_PARUS_RPH1_MAX_TOTAL_SIZE) {
        capacity = RIBON_PARUS_RPH1_MAX_TOTAL_SIZE;
    }
    table_capacity = (uint64_t)RPH1_IMPLEMENTED_SECTION_COUNT *
                     RIBON_PARUS_RPH1_SECTION_ENTRY_SIZE;
    if (rph1_add_overflows(RIBON_PARUS_RPH1_HEADER_SIZE, table_capacity)) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_OUT_OF_CAPACITY;
    }
    memset(buffer, 0, (size_t)capacity);
    writer.bytes = (unsigned char *)buffer;
    writer.capacity = capacity;
    writer.cursor = RIBON_PARUS_RPH1_HEADER_SIZE + table_capacity;
    writer.section_count = 0u;

    status = rph1_append_region_list(
        &writer,
        RIBON_PARUS_RPH1_SECTION_MEMORY_MAP,
        normalized_memory_map,
        0);
    if (status == RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        status = rph1_append_region_list(
            &writer,
            RIBON_PARUS_RPH1_SECTION_RESERVED_RANGES,
            normalized_memory_map,
            1);
    }
    if (status == RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        status = rph1_append_kernel_layout(&writer, plan);
    }
    if (status == RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        status = rph1_append_range_descriptor(
            &writer,
            RIBON_PARUS_RPH1_SECTION_DEVICE_TREE,
            environment->device_tree.physical_address,
            environment->device_tree.size,
            0u,
            0u);
    }
    if (status == RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        status = rph1_append_range_descriptor(
            &writer,
            RIBON_PARUS_RPH1_SECTION_ACPI,
            environment->acpi_rsdp.physical_address,
            environment->acpi_rsdp.size,
            environment->acpi_rsdp.revision,
            0u);
    }
    if (status == RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        status = rph1_append_command_line(&writer, environment);
    }
    if (status == RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        status = rph1_append_framebuffer(&writer, environment);
    }
    if (status == RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        status = rph1_append_modules(&writer, environment);
    }
    if (status == RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        status = rph1_append_boot_media(&writer, environment);
    }
    if (status == RIBON_PROTOCOL_HANDOFF_STATUS_OK) {
        status = rph1_append_provenance(&writer, plan);
    }
    if (status != RIBON_PROTOCOL_HANDOFF_STATUS_OK || writer.cursor > UINT32_MAX) {
        return status != RIBON_PROTOCOL_HANDOFF_STATUS_OK ?
                   status :
                   RIBON_PROTOCOL_HANDOFF_STATUS_OUT_OF_CAPACITY;
    }

    total_size = (uint32_t)writer.cursor;
    rph1_write_u32(buffer, RIBON_PARUS_RPH1_HEADER_MAGIC_OFFSET, RIBON_PARUS_RPH1_MAGIC);
    rph1_write_u16(
        buffer,
        RIBON_PARUS_RPH1_HEADER_VERSION_MAJOR_OFFSET,
        RIBON_PARUS_RPH1_VERSION_MAJOR);
    rph1_write_u16(
        buffer,
        RIBON_PARUS_RPH1_HEADER_VERSION_MINOR_OFFSET,
        RIBON_PARUS_RPH1_VERSION_MINOR);
    rph1_write_u16(buffer, RIBON_PARUS_RPH1_HEADER_SIZE_OFFSET, RIBON_PARUS_RPH1_HEADER_SIZE);
    rph1_write_u16(
        buffer,
        RIBON_PARUS_RPH1_HEADER_SECTION_ENTRY_SIZE_OFFSET,
        RIBON_PARUS_RPH1_SECTION_ENTRY_SIZE);
    rph1_write_u16(
        buffer,
        RIBON_PARUS_RPH1_HEADER_SECTION_COUNT_OFFSET,
        writer.section_count);
    rph1_write_u32(buffer, RIBON_PARUS_RPH1_HEADER_TOTAL_SIZE_OFFSET, total_size);
    rph1_write_u32(
        buffer,
        RIBON_PARUS_RPH1_HEADER_SECTION_TABLE_OFFSET,
        RIBON_PARUS_RPH1_HEADER_SIZE);
    rph1_write_u64(buffer, RIBON_PARUS_RPH1_HEADER_FLAGS_OFFSET, RIBON_PARUS_RPH1_FLAG_NONE);
    checksum = ribon_parus_rph1_crc32c(buffer, total_size);
    rph1_write_u32(buffer, RIBON_PARUS_RPH1_HEADER_CRC32C_OFFSET, checksum);
    if (ribon_parus_parse_rph1(buffer, total_size, &validated_view) !=
            RIBON_PARUS_RPH1_PARSE_OK ||
        validated_view.total_size != total_size ||
        validated_view.section_count != writer.section_count) {
        return RIBON_PROTOCOL_HANDOFF_STATUS_INVALID_PLAN;
    }

    out->data = buffer;
    out->size = total_size;
    out->format = "rph1";
    out->version_major = RIBON_PARUS_RPH1_VERSION_MAJOR;
    out->section_count = writer.section_count;
    return RIBON_PROTOCOL_HANDOFF_STATUS_OK;
}
