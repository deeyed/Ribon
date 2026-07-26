#include <Ribon/ribon.h>
#include <Ribon/uefi_hardening.h>

#include <Uefi.h>
#include <Guid/Acpi.h>
#include <Guid/FileInfo.h>
#include <Guid/Fdt.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SerialIo.h>
#include <Protocol/SimpleFileSystem.h>

#define RIBON_UEFI_MAX_LOAD_SEGMENTS 16u
#define RIBON_UEFI_HANDOFF_BUFFER_SIZE 65536u

EFI_GUID gEfiAcpi10TableGuid = ACPI_10_TABLE_GUID;
EFI_GUID gEfiAcpi20TableGuid = EFI_ACPI_20_TABLE_GUID;
EFI_GUID gEfiFileInfoGuid = EFI_FILE_INFO_ID;
EFI_GUID gFdtTableGuid = FDT_TABLE_GUID;
EFI_GUID gEfiGraphicsOutputProtocolGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
EFI_GUID gEfiLoadedImageProtocolGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
EFI_GUID gEfiSerialIoProtocolGuid = EFI_SERIAL_IO_PROTOCOL_GUID;
EFI_GUID gEfiSerialTerminalDeviceTypeGuid = EFI_SERIAL_TERMINAL_DEVICE_TYPE_GUID;
EFI_GUID gEfiSimpleFileSystemProtocolGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;

struct UefiMemoryCapture {
    EFI_MEMORY_DESCRIPTOR *raw_descriptors;
    UINTN raw_size;
    UINTN raw_capacity;
    UINTN map_key;
    UINTN descriptor_size;
    UINT32 descriptor_version;
    struct RibonMemoryRegion *regions;
    UINTN region_count;
    UINTN region_capacity;
};

struct UefiKernelAllocation {
    EFI_PHYSICAL_ADDRESS base;
    UINTN pages;
};

struct UefiPlatformTableDiagnostics {
    UINTN table_count;
    uint32_t acpi20_seen;
    uint32_t acpi10_seen;
    uint32_t acpi_invalid;
    uint32_t fdt_seen;
    uint32_t fdt_invalid;
};

struct UefiParusBootState {
    struct UefiMemoryCapture final_memory_capture;
    struct UefiKernelAllocation kernel_allocations[RIBON_UEFI_MAX_LOAD_SEGMENTS];
    uint32_t kernel_allocation_count;
    EFI_PHYSICAL_ADDRESS direct_high_page_tables;
    UINTN direct_high_page_count;
    int direct_high_enabled;
    struct RibonArchDirectHighHandoff direct_high;
    struct RibonLoadSegment *kernel_segments;
    struct RibonMemoryRegion *normalized_regions;
    uint32_t normalized_region_capacity;
    unsigned char *handoff_buffer;
    struct RibonHandoffArtifact handoff_artifact;
    struct RibonBootPlan plan;
};

static EFI_SYSTEM_TABLE *g_system_table;
static EFI_BOOT_SERVICES *g_boot_services;
static EFI_SERIAL_IO_PROTOCOL *g_serial;

static UINTN ascii_length(const char *text) {
    UINTN length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static void serial_write_ascii(const char *text) {
    if (g_serial == 0 || g_serial->Write == 0 || text == 0) {
        return;
    }
    UINTN length = ascii_length(text);
    if (length != 0u) {
        (void)g_serial->Write(g_serial, &length, (VOID *)text);
    }
}

static void serial_write_hex64(uint64_t value) {
    char buffer[19];
    buffer[0] = '0';
    buffer[1] = 'x';
    for (uint32_t index = 0; index < 16u; ++index) {
        const uint32_t shift = (15u - index) * 4u;
        const uint8_t digit = (uint8_t)((value >> shift) & 0xfu);
        buffer[2u + index] = (char)(digit < 10u ? ('0' + digit) : ('a' + (digit - 10u)));
    }
    buffer[18] = '\0';
    serial_write_ascii(buffer);
}

static void serial_write_hex_line(const char *prefix, uint64_t value) {
    serial_write_ascii(prefix);
    serial_write_hex64(value);
    serial_write_ascii("\r\n");
}

static void serial_write_ascii_line(const char *prefix, const char *value) {
    serial_write_ascii(prefix);
    serial_write_ascii(value == 0 ? "unknown" : value);
    serial_write_ascii("\r\n");
}

static void uefi_diag_stage(enum RibonUefiDiagnosticStage stage) {
    serial_write_ascii_line("RIBON-UEFI-DIAG-STAGE=", ribon_uefi_diagnostic_stage_name(stage));
}

static void uefi_diag_status(enum RibonUefiDiagnosticStage stage, EFI_STATUS status) {
    serial_write_ascii("RIBON-UEFI-DIAG-STATUS=");
    serial_write_ascii(ribon_uefi_diagnostic_stage_name(stage));
    serial_write_ascii(":");
    serial_write_hex64((uint64_t)status);
    serial_write_ascii("\r\n");
}

static void uefi_diag_counter(enum RibonUefiDiagnosticStage stage, const char *field, uint64_t value) {
    serial_write_ascii("RIBON-UEFI-DIAG-COUNT=");
    serial_write_ascii(ribon_uefi_diagnostic_stage_name(stage));
    serial_write_ascii(":");
    serial_write_ascii(field == 0 ? "value" : field);
    serial_write_ascii("=");
    serial_write_hex64(value);
    serial_write_ascii("\r\n");
}

static void uefi_line(const CHAR16 *wide, const char *ascii) {
    if (g_system_table != 0 && g_system_table->ConOut != 0 &&
        g_system_table->ConOut->OutputString != 0 && wide != 0) {
        (void)g_system_table->ConOut->OutputString(g_system_table->ConOut, (CHAR16 *)wide);
    }
    serial_write_ascii(ascii);
}

static EFI_STATUS allocate_pool(UINTN size, void **out) {
    if (out == 0 || size == 0u) {
        return EFI_INVALID_PARAMETER;
    }
    *out = 0;
    return g_boot_services->AllocatePool(EfiLoaderData, size, (VOID **)out);
}

static void free_pool(void *buffer) {
    if (buffer != 0) {
        (void)g_boot_services->FreePool(buffer);
    }
}

static void release_memory_capture(struct UefiMemoryCapture *capture) {
    if (capture == 0) {
        return;
    }
    free_pool(capture->regions);
    free_pool(capture->raw_descriptors);
    capture->raw_descriptors = 0;
    capture->raw_size = 0;
    capture->raw_capacity = 0;
    capture->map_key = 0;
    capture->descriptor_size = 0;
    capture->descriptor_version = 0;
    capture->regions = 0;
    capture->region_count = 0;
    capture->region_capacity = 0;
}

static uint64_t uefi_max_u64(uint64_t lhs, uint64_t rhs) {
    return lhs > rhs ? lhs : rhs;
}

static int uefi_guid_equals(const EFI_GUID *lhs, const EFI_GUID *rhs) {
    if (lhs == 0 || rhs == 0) {
        return 0;
    }
    return lhs->Data1 == rhs->Data1 &&
           lhs->Data2 == rhs->Data2 &&
           lhs->Data3 == rhs->Data3 &&
           lhs->Data4[0] == rhs->Data4[0] &&
           lhs->Data4[1] == rhs->Data4[1] &&
           lhs->Data4[2] == rhs->Data4[2] &&
           lhs->Data4[3] == rhs->Data4[3] &&
           lhs->Data4[4] == rhs->Data4[4] &&
           lhs->Data4[5] == rhs->Data4[5] &&
           lhs->Data4[6] == rhs->Data4[6] &&
           lhs->Data4[7] == rhs->Data4[7];
}

static uint32_t uefi_read_le32(const unsigned char *bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) |
           ((uint32_t)bytes[3] << 24u);
}

static uint32_t uefi_read_be32(const unsigned char *bytes) {
    return ((uint32_t)bytes[0] << 24u) |
           ((uint32_t)bytes[1] << 16u) |
           ((uint32_t)bytes[2] << 8u) |
           (uint32_t)bytes[3];
}

static void uefi_memzero(void *destination, uint64_t size) {
    volatile unsigned char *bytes = (volatile unsigned char *)destination;
    for (uint64_t index = 0; index < size; ++index) {
        bytes[index] = 0u;
    }
}

static void uefi_memcopy(void *destination, const void *source, uint64_t size) {
    volatile unsigned char *dst = (volatile unsigned char *)destination;
    const volatile unsigned char *src = (const volatile unsigned char *)source;
    for (uint64_t index = 0; index < size; ++index) {
        dst[index] = src[index];
    }
}

static int uefi_range_end(uint64_t base, uint64_t length, uint64_t *out) {
    if (out == 0 || length == 0u || base > UINT64_MAX - length) {
        return 0;
    }
    *out = base + length;
    return 1;
}

static int uefi_range_inside_region(
    const struct RibonMemoryRegion *region,
    uint64_t base,
    uint64_t end) {
    uint64_t region_end = 0;
    if (region == 0 || !uefi_range_end(region->base, region->length, &region_end)) {
        return 0;
    }
    return region->base <= base && end <= region_end;
}

static int uefi_memory_range_is_usable(
    const struct UefiMemoryCapture *capture,
    uint64_t base,
    uint64_t length) {
    uint64_t end = 0;
    if (capture == 0 || capture->regions == 0 || !uefi_range_end(base, length, &end)) {
        return 0;
    }
    for (UINTN index = 0; index < capture->region_count; ++index) {
        const struct RibonMemoryRegion *region = &capture->regions[index];
        if (region->kind == RIBON_MEMORY_REGION_USABLE &&
            uefi_range_inside_region(region, base, end)) {
            return 1;
        }
    }
    return 0;
}

static void uefi_release_kernel_allocations(
    struct UefiKernelAllocation *allocations,
    uint32_t allocation_count) {
    for (uint32_t index = 0; index < allocation_count; ++index) {
        if (allocations[index].base != 0u && allocations[index].pages != 0u) {
            (void)g_boot_services->FreePages(allocations[index].base, allocations[index].pages);
            allocations[index].base = 0u;
            allocations[index].pages = 0u;
        }
    }
}

static void uefi_release_boot_state(struct UefiParusBootState *state) {
    if (state == 0) {
        return;
    }
    uefi_release_kernel_allocations(state->kernel_allocations, state->kernel_allocation_count);
    if (state->direct_high_page_tables != 0u && state->direct_high_page_count != 0u) {
        (void)g_boot_services->FreePages(state->direct_high_page_tables, state->direct_high_page_count);
    }
    release_memory_capture(&state->final_memory_capture);
    free_pool(state->handoff_buffer);
    free_pool(state->normalized_regions);
    free_pool(state->kernel_segments);
    *state = (struct UefiParusBootState){0};
}

static EFI_STATUS uefi_place_kernel_segments(
    const struct UefiMemoryCapture *memory_capture,
    const struct RibonPayloadImage *kernel_payload,
    const struct RibonArchDescriptor *arch,
    struct RibonLoadedPayload *kernel_layout,
    struct UefiKernelAllocation *allocations,
    uint32_t *allocation_count_out) {
    uint64_t runtime_base = UINT64_MAX;
    uint64_t runtime_end = 0;
    int fallback_used = 0;
    int entry_resolved = 0;

    if (memory_capture == 0 || kernel_payload == 0 || kernel_payload->data == 0 || arch == 0 ||
        kernel_layout == 0 || kernel_layout->segments == 0 || allocations == 0 ||
        allocation_count_out == 0 || arch->page_size == 0u) {
        return EFI_INVALID_PARAMETER;
    }
    *allocation_count_out = 0;

    serial_write_hex_line("RIBON-UEFI-KERNEL-SEGMENTS=", kernel_layout->segment_count);
    for (uint32_t index = 0; index < kernel_layout->segment_count; ++index) {
        struct RibonLoadSegment *segment = &kernel_layout->segments[index];
        const uint64_t page_base = ribon_align_down(segment->load_address, arch->page_size);
        uint64_t segment_end = 0;
        uint64_t page_end = 0;
        uint64_t alloc_size = 0;
        UINTN alloc_pages;
        EFI_PHYSICAL_ADDRESS target;
        EFI_STATUS status = EFI_SUCCESS;
        int exact_preferred;
        int exact_allocated = 0;
        const uint64_t page_offset = segment->load_address - page_base;

        if (*allocation_count_out >= RIBON_UEFI_MAX_LOAD_SEGMENTS ||
            !uefi_range_end(segment->load_address, segment->memory_size, &segment_end) ||
            ribon_align_up(segment_end, arch->page_size, &page_end) != RIBON_MEMORY_STATUS_OK ||
            page_end <= page_base) {
            return EFI_LOAD_ERROR;
        }
        alloc_size = page_end - page_base;
        if (alloc_size / arch->page_size > (uint64_t)((UINTN)-1)) {
            return EFI_OUT_OF_RESOURCES;
        }
        alloc_pages = (UINTN)(alloc_size / arch->page_size);
        exact_preferred = uefi_memory_range_is_usable(memory_capture, page_base, alloc_size);

        serial_write_hex_line("RIBON-UEFI-KERNEL-SEGMENT-LOAD=", segment->load_address);
        serial_write_hex_line("RIBON-UEFI-KERNEL-SEGMENT-SIZE=", segment->memory_size);
        if (exact_preferred) {
            target = page_base;
            status = g_boot_services->AllocatePages(AllocateAddress, EfiLoaderCode, alloc_pages, &target);
            exact_allocated = !EFI_ERROR(status) && target == page_base;
            serial_write_hex_line("RIBON-UEFI-KERNEL-PREF-ALLOC-STATUS=", (uint64_t)status);
            if (!exact_allocated) {
                serial_write_hex_line("RIBON-UEFI-KERNEL-PREF-ALLOC-TARGET=", target);
            }
        } else {
            serial_write_hex_line("RIBON-UEFI-KERNEL-PREF-RANGE-BLOCKED=", page_base);
        }
        if (!exact_allocated) {
            target = 0;
            status = g_boot_services->AllocatePages(AllocateAnyPages, EfiLoaderCode, alloc_pages, &target);
            if (EFI_ERROR(status) || target == 0u) {
                serial_write_hex_line("RIBON-UEFI-KERNEL-ALLOC-FAIL=", (uint64_t)status);
                return EFI_OUT_OF_RESOURCES;
            }
            fallback_used = 1;
            serial_write_ascii("RIBON-UEFI-KERNEL-FALLBACK-ALLOC\r\n");
            serial_write_hex_line("RIBON-UEFI-KERNEL-FALLBACK-RUNTIME=", target + page_offset);
        } else {
            serial_write_ascii("RIBON-UEFI-KERNEL-PREF-ALLOC-OK\r\n");
        }

        allocations[*allocation_count_out].base = target;
        allocations[*allocation_count_out].pages = alloc_pages;
        ++(*allocation_count_out);

        uefi_memzero((void *)(uintptr_t)target, alloc_size);
        if (segment->file_offset > kernel_payload->size ||
            segment->file_size > kernel_payload->size - segment->file_offset ||
            segment->file_size > segment->memory_size) {
            return EFI_LOAD_ERROR;
        }
        uefi_memcopy(
            (void *)(uintptr_t)(target + page_offset),
            (const unsigned char *)kernel_payload->data + segment->file_offset,
            segment->file_size);

        segment->runtime_address = target + page_offset;
        if (segment->runtime_address < runtime_base) {
            runtime_base = segment->runtime_address;
        }
        runtime_end = uefi_max_u64(runtime_end, segment->runtime_address + segment->memory_size);
        if (kernel_layout->entry_point >= segment->virtual_address &&
            kernel_layout->entry_point < segment->virtual_address + segment->memory_size &&
            (segment->flags & RIBON_LOAD_SEGMENT_EXECUTE) != 0u) {
            kernel_layout->runtime_entry_address =
                segment->runtime_address + (kernel_layout->entry_point - segment->virtual_address);
            entry_resolved = 1;
        }
        serial_write_hex_line("RIBON-UEFI-KERNEL-SEGMENT-RUNTIME=", segment->runtime_address);
    }

    if (runtime_end <= runtime_base || !entry_resolved) {
        return EFI_LOAD_ERROR;
    }
    kernel_layout->runtime_load_base = runtime_base;
    kernel_layout->runtime_load_end = runtime_end;
    kernel_layout->memory_size = runtime_end - runtime_base;
    kernel_layout->load_plan_flags |= RIBON_LOAD_PLAN_SEGMENTS_PLACED | RIBON_LOAD_PLAN_RUNTIME_ENTRY_VALID;
    if (fallback_used) {
        kernel_layout->load_plan_flags |= RIBON_LOAD_PLAN_FALLBACK_ALLOCATION;
    }
    serial_write_hex_line("RIBON-UEFI-KERNEL-RUNTIME-BASE=", kernel_layout->runtime_load_base);
    serial_write_hex_line("RIBON-UEFI-KERNEL-RUNTIME-END=", kernel_layout->runtime_load_end);
    serial_write_hex_line("RIBON-UEFI-KERNEL-RUNTIME-ENTRY=", kernel_layout->runtime_entry_address);
    return EFI_SUCCESS;
}

static EFI_STATUS uefi_prepare_direct_high(
    const struct RibonLoadedPayload *kernel_layout,
    struct UefiParusBootState *state) {
    const uint64_t pages64 = ribon_arch_direct_high_page_table_pages(kernel_layout);
    EFI_PHYSICAL_ADDRESS table_base = 0;
    EFI_STATUS status;
    int direct_status;

    if (kernel_layout == 0 || state == 0 || pages64 == 0u || pages64 > (uint64_t)((UINTN)-1)) {
        serial_write_ascii("RIBON-UEFI-DIRECT-HIGH-UNSUPPORTED\r\n");
        return EFI_UNSUPPORTED;
    }

    status = g_boot_services->AllocatePages(
        AllocateAnyPages,
        EfiLoaderData,
        (UINTN)pages64,
        &table_base);
    if (EFI_ERROR(status) || table_base == 0u) {
        serial_write_hex_line("RIBON-UEFI-DIRECT-HIGH-ALLOC-STATUS=", (uint64_t)status);
        return EFI_OUT_OF_RESOURCES;
    }

    direct_status = ribon_arch_prepare_direct_high_entry(
        kernel_layout,
        table_base,
        (void *)(uintptr_t)table_base,
        pages64 * 4096ull,
        &state->direct_high);
    if (direct_status != RIBON_ARCH_DIRECT_HIGH_OK) {
        serial_write_hex_line("RIBON-UEFI-DIRECT-HIGH-STATUS=", (uint64_t)(int64_t)direct_status);
        (void)g_boot_services->FreePages(table_base, (UINTN)pages64);
        return EFI_LOAD_ERROR;
    }

    state->direct_high_page_tables = table_base;
    state->direct_high_page_count = (UINTN)pages64;
    state->direct_high_enabled = 1;
    serial_write_ascii("RIBON-UEFI-DIRECT-HIGH-PREPARED\r\n");
    serial_write_hex_line("RIBON-UEFI-DIRECT-HIGH-ENTRY=", state->direct_high.entry);
    serial_write_hex_line("RIBON-UEFI-DIRECT-HIGH-CR3=", state->direct_high.bootstrap0);
    serial_write_hex_line("RIBON-UEFI-DIRECT-HIGH-VADDR-START=", state->direct_high.high_vaddr_start);
    serial_write_hex_line("RIBON-UEFI-DIRECT-HIGH-LOAD-START=", state->direct_high.high_load_start);
    return EFI_SUCCESS;
}

static enum RibonMemoryRegionKind uefi_region_kind(UINT32 type) {
    switch (type) {
    case EfiConventionalMemory:
        return RIBON_MEMORY_REGION_USABLE;
    case EfiLoaderCode:
    case EfiLoaderData:
        return RIBON_MEMORY_REGION_BOOTLOADER;
    case EfiBootServicesCode:
    case EfiBootServicesData:
    case EfiRuntimeServicesCode:
    case EfiRuntimeServicesData:
        return RIBON_MEMORY_REGION_FIRMWARE;
    case EfiACPIReclaimMemory:
    case EfiACPIMemoryNVS:
        return RIBON_MEMORY_REGION_ACPI;
    case EfiMemoryMappedIO:
    case EfiMemoryMappedIOPortSpace:
        return RIBON_MEMORY_REGION_MMIO;
    case EfiReservedMemoryType:
    case EfiUnusableMemory:
    case EfiPalCode:
    case EfiPersistentMemory:
    default:
        return RIBON_MEMORY_REGION_RESERVED;
    }
}

static uint64_t uefi_region_attributes(const EFI_MEMORY_DESCRIPTOR *descriptor) {
    uint64_t attributes = RIBON_MEMORY_ATTR_READ | RIBON_MEMORY_ATTR_WRITE;
    if (descriptor->Type == EfiMemoryMappedIO || descriptor->Type == EfiMemoryMappedIOPortSpace) {
        attributes |= RIBON_MEMORY_ATTR_DEVICE;
    }
    if (descriptor->Type == EfiBootServicesCode || descriptor->Type == EfiBootServicesData ||
        descriptor->Type == EfiLoaderCode || descriptor->Type == EfiLoaderData) {
        attributes |= RIBON_MEMORY_ATTR_BOOT_RECLAIMABLE;
    }
    if (descriptor->Type == EfiRuntimeServicesCode || descriptor->Type == EfiRuntimeServicesData ||
        (descriptor->Attribute & EFI_MEMORY_RUNTIME) != 0u) {
        attributes |= RIBON_MEMORY_ATTR_FIRMWARE_RUNTIME;
    }
    if (descriptor->Type == EfiLoaderCode || descriptor->Type == EfiBootServicesCode ||
        descriptor->Type == EfiRuntimeServicesCode) {
        attributes |= RIBON_MEMORY_ATTR_EXECUTE;
    }
    return attributes;
}

static void populate_memory_regions(struct UefiMemoryCapture *capture) {
    const UINTN descriptor_count = capture->raw_size / capture->descriptor_size;
    for (UINTN index = 0; index < descriptor_count; ++index) {
        const EFI_MEMORY_DESCRIPTOR *descriptor =
            (const EFI_MEMORY_DESCRIPTOR *)((const UINT8 *)capture->raw_descriptors + index * capture->descriptor_size);
        capture->regions[index].base = descriptor->PhysicalStart;
        capture->regions[index].length = descriptor->NumberOfPages * 4096ull;
        capture->regions[index].kind = uefi_region_kind(descriptor->Type);
        capture->regions[index].attributes = uefi_region_attributes(descriptor);
    }
    capture->region_count = descriptor_count;
}

static EFI_STATUS refresh_memory_capture(struct UefiMemoryCapture *capture) {
    EFI_STATUS status;
    UINTN map_size;
    UINTN map_key = 0;
    UINTN descriptor_size;
    UINT32 descriptor_version;
    int fit_status;

    if (capture == 0 || capture->raw_descriptors == 0 || capture->raw_capacity == 0u ||
        capture->regions == 0 || capture->region_capacity == 0u) {
        return EFI_INVALID_PARAMETER;
    }
    map_size = capture->raw_capacity;
    descriptor_size = capture->descriptor_size;
    descriptor_version = capture->descriptor_version;
    status = g_boot_services->GetMemoryMap(
        &map_size,
        capture->raw_descriptors,
        &map_key,
        &descriptor_size,
        &descriptor_version);
    if (EFI_ERROR(status)) {
        capture->raw_size = map_size;
        return status;
    }
    fit_status = ribon_uefi_memory_map_refresh_fits(
        (uint64_t)map_size,
        (uint64_t)descriptor_size,
        (uint64_t)capture->raw_capacity,
        (uint64_t)capture->region_capacity);
    if (fit_status != RIBON_UEFI_HARDENING_OK) {
        return EFI_OUT_OF_RESOURCES;
    }
    capture->raw_size = map_size;
    capture->map_key = map_key;
    capture->descriptor_size = descriptor_size;
    capture->descriptor_version = descriptor_version;
    populate_memory_regions(capture);
    return EFI_SUCCESS;
}

static EFI_STATUS allocate_memory_capture_buffers(
    struct UefiMemoryCapture *capture,
    uint64_t raw_capacity,
    uint64_t descriptor_size,
    UINT32 descriptor_version) {
    const uint64_t descriptor_capacity = ribon_uefi_descriptor_capacity(raw_capacity, descriptor_size);
    EFI_STATUS status;
    if (capture == 0 || raw_capacity == 0u || descriptor_size == 0u || descriptor_capacity == 0u ||
        raw_capacity > (uint64_t)((UINTN)-1) ||
        descriptor_capacity > (uint64_t)((UINTN)-1) / sizeof(struct RibonMemoryRegion)) {
        return EFI_OUT_OF_RESOURCES;
    }
    status = allocate_pool((UINTN)raw_capacity, (void **)&capture->raw_descriptors);
    if (EFI_ERROR(status)) {
        return status;
    }
    status = allocate_pool(
        (UINTN)(descriptor_capacity * sizeof(struct RibonMemoryRegion)),
        (void **)&capture->regions);
    if (EFI_ERROR(status)) {
        release_memory_capture(capture);
        return status;
    }
    capture->raw_capacity = (UINTN)raw_capacity;
    capture->descriptor_size = (UINTN)descriptor_size;
    capture->descriptor_version = descriptor_version;
    capture->region_capacity = (UINTN)descriptor_capacity;
    return EFI_SUCCESS;
}

static EFI_STATUS capture_memory_map(struct UefiMemoryCapture *capture) {
    EFI_STATUS status;
    UINTN map_size = 0;
    UINTN map_key = 0;
    UINTN descriptor_size = 0;
    UINT32 descriptor_version = 0;
    uint64_t requested_size = 0;

    if (capture == 0) {
        return EFI_INVALID_PARAMETER;
    }
    release_memory_capture(capture);
    status = g_boot_services->GetMemoryMap(
        &map_size,
        0,
        &map_key,
        &descriptor_size,
        &descriptor_version);
    if (status != EFI_BUFFER_TOO_SMALL || descriptor_size == 0u) {
        return status == EFI_SUCCESS ? EFI_INVALID_PARAMETER : status;
    }
    requested_size = (uint64_t)map_size;
    serial_write_hex_line("RIBON-UEFI-MEMORY-MAP-PROBE-SIZE=", requested_size);
    serial_write_hex_line("RIBON-UEFI-MEMORY-MAP-DESC-SIZE=", (uint64_t)descriptor_size);
    for (uint32_t attempt = 0; attempt < RIBON_UEFI_MEMORY_MAP_CAPTURE_ATTEMPTS; ++attempt) {
        uint64_t raw_capacity = 0;
        const int capacity_status = ribon_uefi_memory_map_capacity(
            requested_size,
            (uint64_t)descriptor_size,
            &raw_capacity);
        release_memory_capture(capture);
        serial_write_hex_line("RIBON-UEFI-MEMORY-MAP-CAPTURE-ATTEMPT=", attempt + 1u);
        if (capacity_status != RIBON_UEFI_HARDENING_OK) {
            return EFI_OUT_OF_RESOURCES;
        }
        serial_write_hex_line("RIBON-UEFI-MEMORY-MAP-CAPACITY=", raw_capacity);
        status = allocate_memory_capture_buffers(capture, raw_capacity, descriptor_size, descriptor_version);
        if (EFI_ERROR(status)) {
            return status;
        }
        status = refresh_memory_capture(capture);
        serial_write_hex_line("RIBON-UEFI-MEMORY-MAP-REFRESH-STATUS=", (uint64_t)status);
        if (!EFI_ERROR(status)) {
            serial_write_hex_line("RIBON-UEFI-MEMORY-MAP-DESCRIPTORS=", capture->region_count);
            serial_write_hex_line("RIBON-UEFI-MEMORY-MAP-KEY=", capture->map_key);
            return EFI_SUCCESS;
        }
        if (status != EFI_BUFFER_TOO_SMALL || capture->raw_size <= requested_size) {
            release_memory_capture(capture);
            return status;
        }
        requested_size = (uint64_t)capture->raw_size;
        serial_write_hex_line("RIBON-UEFI-MEMORY-MAP-CAPACITY-RETRY-SIZE=", requested_size);
    }
    release_memory_capture(capture);
    return EFI_BUFFER_TOO_SMALL;
}

static EFI_STATUS open_boot_volume(EFI_HANDLE image_handle, EFI_FILE_PROTOCOL **root_out) {
    EFI_STATUS status;
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *file_system = 0;

    if (root_out == 0) {
        return EFI_INVALID_PARAMETER;
    }
    *root_out = 0;
    status = g_boot_services->HandleProtocol(
        image_handle,
        &gEfiLoadedImageProtocolGuid,
        (VOID **)&loaded_image);
    if (EFI_ERROR(status)) {
        return status;
    }
    status = g_boot_services->HandleProtocol(
        loaded_image->DeviceHandle,
        &gEfiSimpleFileSystemProtocolGuid,
        (VOID **)&file_system);
    if (EFI_ERROR(status)) {
        return status;
    }
    return file_system->OpenVolume(file_system, root_out);
}

static EFI_STATUS read_file_from_root(
    EFI_FILE_PROTOCOL *root,
    const CHAR16 *path,
    void **buffer_out,
    UINTN *size_out) {
    EFI_STATUS status;
    EFI_FILE_PROTOCOL *file = 0;
    EFI_FILE_INFO *info = 0;
    UINTN info_size = 0;
    void *buffer = 0;

    if (root == 0 || path == 0 || buffer_out == 0 || size_out == 0) {
        return EFI_INVALID_PARAMETER;
    }
    *buffer_out = 0;
    *size_out = 0;
    status = root->Open(root, &file, (CHAR16 *)path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) {
        return status;
    }

    status = file->GetInfo(file, &gEfiFileInfoGuid, &info_size, info);
    if (status != EFI_BUFFER_TOO_SMALL || info_size == 0u) {
        (void)file->Close(file);
        return status == EFI_SUCCESS ? EFI_INVALID_PARAMETER : status;
    }
    status = allocate_pool(info_size, (void **)&info);
    if (EFI_ERROR(status)) {
        (void)file->Close(file);
        return status;
    }
    status = file->GetInfo(file, &gEfiFileInfoGuid, &info_size, info);
    if (EFI_ERROR(status) || info->FileSize == 0u) {
        free_pool(info);
        (void)file->Close(file);
        return EFI_ERROR(status) ? status : EFI_NOT_FOUND;
    }
    if (info->FileSize > (UINT64)((UINTN)-1)) {
        free_pool(info);
        (void)file->Close(file);
        return EFI_OUT_OF_RESOURCES;
    }

    UINTN read_size = (UINTN)info->FileSize;
    status = allocate_pool(read_size, &buffer);
    if (EFI_ERROR(status)) {
        free_pool(info);
        (void)file->Close(file);
        return status;
    }
    status = file->Read(file, &read_size, buffer);
    free_pool(info);
    (void)file->Close(file);
    if (EFI_ERROR(status)) {
        free_pool(buffer);
        return status;
    }
    if (read_size == 0u) {
        free_pool(buffer);
        return EFI_NOT_FOUND;
    }

    *buffer_out = buffer;
    *size_out = read_size;
    return EFI_SUCCESS;
}

static int boot_option_file_exists(EFI_FILE_PROTOCOL *root, const CHAR16 *path) {
    EFI_FILE_PROTOCOL *file = 0;
    EFI_STATUS status;
    if (root == 0 || path == 0) {
        return 0;
    }
    status = root->Open(root, &file, (CHAR16 *)path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status) || file == 0) {
        return 0;
    }
    (void)file->Close(file);
    return 1;
}

static EFI_STATUS read_kernel_payload(
    EFI_FILE_PROTOCOL *root,
    struct RibonPayloadImage *payload,
    void **kernel_buffer_out) {
    static const CHAR16 *paths[] = {
        L"\\kernel\\kernel.elf",
        L"kernel\\kernel.elf",
        L"kernel.elf",
    };
    EFI_STATUS last_status = EFI_NOT_FOUND;
    for (UINTN index = 0; index < (sizeof(paths) / sizeof(paths[0])); ++index) {
        void *buffer = 0;
        UINTN size = 0;
        const EFI_STATUS status = read_file_from_root(root, paths[index], &buffer, &size);
        if (!EFI_ERROR(status)) {
            payload->data = buffer;
            payload->size = (uint64_t)size;
            payload->source_name = "kernel/kernel.elf";
            *kernel_buffer_out = buffer;
            return EFI_SUCCESS;
        }
        last_status = status;
    }
    return last_status;
}

static EFI_STATUS collect_framebuffer(struct RibonBootEnvironment *environment) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    EFI_STATUS status = g_boot_services->LocateProtocol(
        &gEfiGraphicsOutputProtocolGuid,
        0,
        (VOID **)&gop);
    if (EFI_ERROR(status)) {
        return status;
    }
    if (gop == 0 || gop->Mode == 0 || gop->Mode->Info == 0 ||
        gop->Mode->FrameBufferBase == 0u ||
        gop->Mode->Info->PixelFormat == PixelBltOnly) {
        return EFI_UNSUPPORTED;
    }
    environment->framebuffer.physical_address = gop->Mode->FrameBufferBase;
    environment->framebuffer.width = gop->Mode->Info->HorizontalResolution;
    environment->framebuffer.height = gop->Mode->Info->VerticalResolution;
    environment->framebuffer.pitch = gop->Mode->Info->PixelsPerScanLine * 4u;
    environment->framebuffer.bits_per_pixel = 32u;
    environment->framebuffer.backend = RIBON_FRAMEBUFFER_BACKEND_UEFI_GOP;
    switch (gop->Mode->Info->PixelFormat) {
    case PixelBlueGreenRedReserved8BitPerColor:
        environment->framebuffer.rgb.red_position = 16u;
        environment->framebuffer.rgb.red_mask_size = 8u;
        environment->framebuffer.rgb.green_position = 8u;
        environment->framebuffer.rgb.green_mask_size = 8u;
        environment->framebuffer.rgb.blue_position = 0u;
        environment->framebuffer.rgb.blue_mask_size = 8u;
        break;
    case PixelRedGreenBlueReserved8BitPerColor:
        environment->framebuffer.rgb.red_position = 0u;
        environment->framebuffer.rgb.red_mask_size = 8u;
        environment->framebuffer.rgb.green_position = 8u;
        environment->framebuffer.rgb.green_mask_size = 8u;
        environment->framebuffer.rgb.blue_position = 16u;
        environment->framebuffer.rgb.blue_mask_size = 8u;
        break;
    default:
        environment->framebuffer.rgb.red_position = 0u;
        environment->framebuffer.rgb.red_mask_size = 8u;
        environment->framebuffer.rgb.green_position = 8u;
        environment->framebuffer.rgb.green_mask_size = 8u;
        environment->framebuffer.rgb.blue_position = 16u;
        environment->framebuffer.rgb.blue_mask_size = 8u;
        break;
    }
    environment->flags |= RIBON_BOOT_ENV_HAS_FRAMEBUFFER;
    return EFI_SUCCESS;
}

static int uefi_checksum_is_zero(const unsigned char *bytes, uint32_t size) {
    uint8_t sum = 0;
    if (bytes == 0 || size == 0u) {
        return 0;
    }
    for (uint32_t index = 0; index < size; ++index) {
        sum = (uint8_t)(sum + bytes[index]);
    }
    return sum == 0u;
}

static int uefi_rsdp_signature_is_valid(const unsigned char *rsdp) {
    static const unsigned char signature[8] = {'R', 'S', 'D', ' ', 'P', 'T', 'R', ' '};
    if (rsdp == 0) {
        return 0;
    }
    for (uint32_t index = 0; index < sizeof(signature); ++index) {
        if (rsdp[index] != signature[index]) {
            return 0;
        }
    }
    return 1;
}

static int uefi_validate_rsdp(
    const void *table,
    uint32_t minimum_revision,
    uint32_t *size_out,
    uint32_t *revision_out) {
    const unsigned char *rsdp = (const unsigned char *)table;
    uint32_t length = 20u;
    uint32_t revision;
    if (size_out == 0 || revision_out == 0 || rsdp == 0 || !uefi_rsdp_signature_is_valid(rsdp) ||
        !uefi_checksum_is_zero(rsdp, 20u)) {
        return 0;
    }
    revision = rsdp[15];
    if (revision < minimum_revision) {
        return 0;
    }
    if (revision >= 2u) {
        const uint32_t table_length = uefi_read_le32(rsdp + 20u);
        if (table_length < 36u || table_length > 4096u ||
            !uefi_checksum_is_zero(rsdp, table_length)) {
            return 0;
        }
        length = table_length;
    }
    *size_out = length;
    *revision_out = revision;
    return 1;
}

static void collect_platform_tables(
    struct RibonBootEnvironment *environment,
    struct UefiPlatformTableDiagnostics *diagnostics) {
    if (environment == 0 || g_system_table == 0 || g_system_table->ConfigurationTable == 0) {
        return;
    }
    if (diagnostics != 0) {
        diagnostics->table_count = g_system_table->NumberOfTableEntries;
    }
    for (UINTN index = 0; index < g_system_table->NumberOfTableEntries; ++index) {
        const EFI_CONFIGURATION_TABLE *table = &g_system_table->ConfigurationTable[index];
        if (environment->acpi_rsdp.data == 0 &&
            uefi_guid_equals(&table->VendorGuid, &gEfiAcpi20TableGuid) &&
            table->VendorTable != 0) {
            uint32_t length = 0;
            uint32_t revision = 0;
            if (diagnostics != 0) {
                ++diagnostics->acpi20_seen;
            }
            if (!uefi_validate_rsdp(table->VendorTable, 2u, &length, &revision)) {
                if (diagnostics != 0) {
                    ++diagnostics->acpi_invalid;
                }
                continue;
            }
            environment->acpi_rsdp.physical_address = (uint64_t)(uintptr_t)table->VendorTable;
            environment->acpi_rsdp.data = table->VendorTable;
            environment->acpi_rsdp.size = length;
            environment->acpi_rsdp.revision = revision;
            environment->flags |= RIBON_BOOT_ENV_HAS_ACPI;
            continue;
        }
        if (environment->acpi_rsdp.data == 0 &&
            uefi_guid_equals(&table->VendorGuid, &gEfiAcpi10TableGuid) &&
            table->VendorTable != 0) {
            uint32_t length = 0;
            uint32_t revision = 0;
            if (diagnostics != 0) {
                ++diagnostics->acpi10_seen;
            }
            if (!uefi_validate_rsdp(table->VendorTable, 0u, &length, &revision)) {
                if (diagnostics != 0) {
                    ++diagnostics->acpi_invalid;
                }
                continue;
            }
            environment->acpi_rsdp.physical_address = (uint64_t)(uintptr_t)table->VendorTable;
            environment->acpi_rsdp.data = table->VendorTable;
            environment->acpi_rsdp.size = length;
            environment->acpi_rsdp.revision = revision;
            environment->flags |= RIBON_BOOT_ENV_HAS_ACPI;
            continue;
        }
        if ((environment->flags & RIBON_BOOT_ENV_HAS_DEVICE_TREE) == 0u &&
            uefi_guid_equals(&table->VendorGuid, &gFdtTableGuid) &&
            table->VendorTable != 0) {
            const unsigned char *fdt = (const unsigned char *)table->VendorTable;
            const uint32_t magic = uefi_read_be32(fdt);
            const uint32_t totalsize = uefi_read_be32(fdt + 4u);
            if (diagnostics != 0) {
                ++diagnostics->fdt_seen;
            }
            if (magic != 0xd00dfeedu || totalsize < 40u || totalsize > 0x01000000u) {
                if (diagnostics != 0) {
                    ++diagnostics->fdt_invalid;
                }
                continue;
            }
            environment->device_tree.physical_address = (uint64_t)(uintptr_t)table->VendorTable;
            environment->device_tree.size = totalsize;
            environment->device_tree.data = table->VendorTable;
            environment->flags |= RIBON_BOOT_ENV_HAS_DEVICE_TREE;
        }
    }
}

static void report_platform_table_diagnostics(
    const struct RibonBootEnvironment *environment,
    const struct UefiPlatformTableDiagnostics *diagnostics) {
    if (diagnostics == 0) {
        return;
    }
    uefi_diag_counter(RIBON_UEFI_DIAG_PLATFORM_TABLES, "entries", diagnostics->table_count);
    uefi_diag_counter(RIBON_UEFI_DIAG_PLATFORM_TABLES, "acpi20", diagnostics->acpi20_seen);
    uefi_diag_counter(RIBON_UEFI_DIAG_PLATFORM_TABLES, "acpi10", diagnostics->acpi10_seen);
    uefi_diag_counter(RIBON_UEFI_DIAG_PLATFORM_TABLES, "acpi-invalid", diagnostics->acpi_invalid);
    uefi_diag_counter(RIBON_UEFI_DIAG_PLATFORM_TABLES, "fdt", diagnostics->fdt_seen);
    uefi_diag_counter(RIBON_UEFI_DIAG_PLATFORM_TABLES, "fdt-invalid", diagnostics->fdt_invalid);
    if (environment != 0 && (environment->flags & RIBON_BOOT_ENV_HAS_ACPI) != 0u) {
        serial_write_hex_line("RIBON-UEFI-ACPI-RSDP=", environment->acpi_rsdp.physical_address);
        serial_write_hex_line("RIBON-UEFI-ACPI-RSDP-SIZE=", environment->acpi_rsdp.size);
    } else {
        serial_write_ascii("RIBON-UEFI-ACPI-RSDP-MISSING\r\n");
    }
    if (environment != 0 && (environment->flags & RIBON_BOOT_ENV_HAS_DEVICE_TREE) != 0u) {
        serial_write_hex_line("RIBON-UEFI-DTB=", environment->device_tree.physical_address);
        serial_write_hex_line("RIBON-UEFI-DTB-SIZE=", environment->device_tree.size);
    } else {
        serial_write_ascii("RIBON-UEFI-DTB-MISSING\r\n");
    }
}

static uint32_t count_command_line(const char *text) {
    uint32_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static EFI_STATUS build_parus_plan_from_capture(
    struct RibonBootEnvironment *environment,
    const struct RibonPayloadImage *kernel_payload,
    struct RibonLoadedPayload *kernel_layout,
    struct UefiParusBootState *state,
    const struct RibonProfile *profile) {
    struct RibonBootEnvironment plan_environment;
    struct RibonMutableMemoryMap normalized_memory_map;
    struct RibonBootRequest request;
    int plan_status;
    plan_environment = *environment;
    plan_environment.memory_map.regions = state->final_memory_capture.regions;
    plan_environment.memory_map.region_count = (uint32_t)state->final_memory_capture.region_count;
    plan_environment.raw_memory_map.data = state->final_memory_capture.raw_descriptors;
    plan_environment.raw_memory_map.size = (uint64_t)state->final_memory_capture.raw_size;
    plan_environment.raw_memory_map.descriptor_size = (uint32_t)state->final_memory_capture.descriptor_size;
    plan_environment.raw_memory_map.descriptor_version = state->final_memory_capture.descriptor_version;
    plan_environment.flags |= RIBON_BOOT_ENV_HAS_RAW_MEMORY_MAP;

    normalized_memory_map = (struct RibonMutableMemoryMap){
        .regions = state->normalized_regions,
        .region_count = 0,
        .capacity = state->normalized_region_capacity,
    };
    state->handoff_artifact = (struct RibonHandoffArtifact){0};
    state->plan = (struct RibonBootPlan){0};
    request = (struct RibonBootRequest){
        .environment = &plan_environment,
        .profile = profile,
        .normalized_memory_map = &normalized_memory_map,
        .kernel_payload = kernel_payload,
        .kernel_layout = kernel_layout,
        .handoff_buffer = state->handoff_buffer,
        .handoff_buffer_capacity = RIBON_UEFI_HANDOFF_BUFFER_SIZE,
        .handoff_artifact = &state->handoff_artifact,
    };
    plan_status = ribon_build_plan(&request, &state->plan);
    if (plan_status != RIBON_STATUS_OK) {
        serial_write_hex_line("RIBON-UEFI-PLAN-STATUS=", (uint64_t)(int64_t)plan_status);
        return EFI_LOAD_ERROR;
    }
    serial_write_hex_line("RIBON-UEFI-RPH1-SIZE=", state->plan.handoff_artifact_size);
    serial_write_hex_line("RIBON-UEFI-RPH1-SECTIONS=", state->plan.handoff_artifact_sections);
    return EFI_SUCCESS;
}

static EFI_STATUS exit_boot_services_with_retry(
    EFI_HANDLE image_handle,
    struct RibonBootEnvironment *environment,
    const struct RibonPayloadImage *kernel_payload,
    struct RibonLoadedPayload *kernel_layout,
    struct UefiParusBootState *state,
    const struct RibonProfile *profile) {
    EFI_STATUS status = EFI_SUCCESS;
    for (uint32_t attempt = 0; attempt < RIBON_UEFI_EXIT_BOOT_SERVICES_MAX_ATTEMPTS; ++attempt) {
        uefi_diag_stage(RIBON_UEFI_DIAG_FINAL_MEMORY_MAP);
        status = refresh_memory_capture(&state->final_memory_capture);
        if (status == EFI_BUFFER_TOO_SMALL) {
            serial_write_hex_line("RIBON-UEFI-FINAL-MEMORY-MAP-GROW-REQUIRED=", state->final_memory_capture.raw_size);
            status = capture_memory_map(&state->final_memory_capture);
        }
        if (EFI_ERROR(status)) {
            serial_write_hex_line("RIBON-UEFI-FINAL-MEMORY-MAP-STATUS=", (uint64_t)status);
            uefi_diag_status(RIBON_UEFI_DIAG_FINAL_MEMORY_MAP, status);
            return status;
        }
        serial_write_ascii("RIBON-UEFI-FINAL-MEMORY-MAP\r\n");
        serial_write_hex_line("RIBON-UEFI-FINAL-MEMORY-MAP-DESCRIPTORS=", state->final_memory_capture.region_count);
        serial_write_hex_line("RIBON-UEFI-FINAL-MEMORY-MAP-KEY=", state->final_memory_capture.map_key);

        uefi_diag_stage(RIBON_UEFI_DIAG_RPH1_REBUILD);
        serial_write_hex_line("RIBON-UEFI-RPH1-REBUILD-ATTEMPT=", attempt + 1u);
        status = build_parus_plan_from_capture(environment, kernel_payload, kernel_layout, state, profile);
        if (EFI_ERROR(status)) {
            uefi_diag_status(RIBON_UEFI_DIAG_RPH1_REBUILD, status);
            return status;
        }
        if (state->plan.kernel_runtime_entry_address == 0u || state->handoff_artifact.data == 0) {
            uefi_diag_status(RIBON_UEFI_DIAG_RPH1_REBUILD, EFI_LOAD_ERROR);
            return EFI_LOAD_ERROR;
        }

        uefi_diag_stage(RIBON_UEFI_DIAG_JUMP);
        serial_write_hex_line(
            "RIBON-UEFI-JUMP-ENTRY=",
            state->direct_high_enabled ?
                state->direct_high.entry :
                state->plan.kernel_runtime_entry_address);
        serial_write_hex_line(
            "RIBON-UEFI-JUMP-FLAGS=",
            state->direct_high_enabled ?
                state->direct_high.entry_flags :
                RIBON_KERNEL_ENTRY_FLAG_RPH1);
        serial_write_hex_line("RIBON-UEFI-HANDOFF=", (uint64_t)(uintptr_t)state->handoff_artifact.data);
        serial_write_hex_line("RIBON-UEFI-EXIT-BOOT-SERVICES-ATTEMPT=", attempt + 1u);
        uefi_diag_stage(RIBON_UEFI_DIAG_EXIT_BOOT_SERVICES);
        serial_write_ascii("RIBON-UEFI-EXIT-BOOT-SERVICES-START\r\n");
        status = g_boot_services->ExitBootServices(image_handle, state->final_memory_capture.map_key);
        if (!EFI_ERROR(status)) {
            return EFI_SUCCESS;
        }
        serial_write_hex_line("RIBON-UEFI-EXIT-BOOT-SERVICES-STATUS=", (uint64_t)status);
        uefi_diag_status(RIBON_UEFI_DIAG_EXIT_BOOT_SERVICES, status);
    }
    return status;
}

static EFI_STATUS boot_parus(
    EFI_HANDLE image_handle,
    struct RibonBootEnvironment *environment,
    const struct RibonPayloadImage *kernel_payload,
    const struct UefiMemoryCapture *memory_capture,
    const struct RibonProfile *profile,
    int direct_high_required) {
    EFI_STATUS status;
    struct UefiParusBootState state = {0};
    struct RibonLoadedPayload kernel_layout = {0};
    int loader_status;
    uint32_t normalized_capacity;

    if (profile == 0) {
        return EFI_UNSUPPORTED;
    }

    status = allocate_pool(
        RIBON_UEFI_MAX_LOAD_SEGMENTS * sizeof(struct RibonLoadSegment),
        (void **)&state.kernel_segments);
    if (EFI_ERROR(status)) {
        return status;
    }
    kernel_layout.segments = state.kernel_segments;
    kernel_layout.segment_capacity = RIBON_UEFI_MAX_LOAD_SEGMENTS;

    uefi_diag_stage(RIBON_UEFI_DIAG_KERNEL_LOAD);
    serial_write_ascii("RIBON-UEFI-KERNEL-LOAD-START\r\n");
    loader_status = ribon_loader_analyze(kernel_payload, environment->arch, &kernel_layout);
    if (loader_status != RIBON_LOADER_STATUS_OK) {
        serial_write_hex_line("RIBON-UEFI-LOADER-STATUS=", (uint64_t)(int64_t)loader_status);
        uefi_diag_status(RIBON_UEFI_DIAG_KERNEL_LOAD, EFI_LOAD_ERROR);
        uefi_release_boot_state(&state);
        return EFI_LOAD_ERROR;
    }
    status = uefi_place_kernel_segments(
        memory_capture,
        kernel_payload,
        environment->arch,
        &kernel_layout,
        state.kernel_allocations,
        &state.kernel_allocation_count);
    if (EFI_ERROR(status)) {
        serial_write_hex_line("RIBON-UEFI-KERNEL-LOAD-STATUS=", (uint64_t)status);
        uefi_diag_status(RIBON_UEFI_DIAG_KERNEL_LOAD, status);
        uefi_release_boot_state(&state);
        return status;
    }
    serial_write_ascii("RIBON-UEFI-KERNEL-LOAD-OK\r\n");
    if (direct_high_required) {
        uefi_diag_stage(RIBON_UEFI_DIAG_DIRECT_HIGH);
        serial_write_ascii("RIBON-UEFI-DIRECT-HIGH-REQUESTED\r\n");
        status = uefi_prepare_direct_high(&kernel_layout, &state);
        if (EFI_ERROR(status)) {
            uefi_diag_status(RIBON_UEFI_DIAG_DIRECT_HIGH, status);
            uefi_release_boot_state(&state);
            return status;
        }
    }

    uefi_diag_stage(RIBON_UEFI_DIAG_FINAL_MEMORY_MAP);
    status = capture_memory_map(&state.final_memory_capture);
    if (EFI_ERROR(status)) {
        serial_write_hex_line("RIBON-UEFI-FINAL-MEMORY-MAP-STATUS=", (uint64_t)status);
        uefi_diag_status(RIBON_UEFI_DIAG_FINAL_MEMORY_MAP, status);
        uefi_release_boot_state(&state);
        return status;
    }
    normalized_capacity = (uint32_t)state.final_memory_capture.region_capacity + 8u;
    status = allocate_pool(
        normalized_capacity * sizeof(struct RibonMemoryRegion),
        (void **)&state.normalized_regions);
    if (EFI_ERROR(status)) {
        uefi_release_boot_state(&state);
        return status;
    }
    state.normalized_region_capacity = normalized_capacity;
    status = allocate_pool(RIBON_UEFI_HANDOFF_BUFFER_SIZE, (void **)&state.handoff_buffer);
    if (EFI_ERROR(status)) {
        uefi_release_boot_state(&state);
        return status;
    }

    status = exit_boot_services_with_retry(
        image_handle,
        environment,
        kernel_payload,
        &kernel_layout,
        &state,
        profile);
    if (EFI_ERROR(status)) {
        uefi_release_boot_state(&state);
        return status;
    }
    ribon_arch_enter_kernel(
        state.direct_high_enabled ? state.direct_high.entry : state.plan.kernel_runtime_entry_address,
        (uint64_t)(uintptr_t)state.handoff_artifact.data,
        state.direct_high_enabled ?
            state.direct_high.entry_flags :
            RIBON_KERNEL_ENTRY_FLAG_RPH1,
        state.direct_high_enabled ? state.direct_high.bootstrap0 : 0u);
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    EFI_STATUS status;
    EFI_FILE_PROTOCOL *root = 0;
    void *kernel_buffer = 0;
    int direct_high_required = 0;
    int gop_required = 0;
    const struct RibonProfile *profile = 0;
    struct UefiMemoryCapture memory_capture = {0};
    struct RibonPayloadImage kernel_payload = {0};
    struct RibonBootEnvironment environment;
    struct UefiPlatformTableDiagnostics table_diagnostics = {0};
    static const char command_line[] = "profile=parus firmware=uefi";

    g_system_table = system_table;
    g_boot_services = system_table->BootServices;
    (void)g_boot_services->LocateProtocol(&gEfiSerialIoProtocolGuid, 0, (VOID **)&g_serial);
    uefi_line(L"RIBON-UEFI-START\r\n", "RIBON-UEFI-START\r\n");

    profile = ribon_find_builtin_profile("parus");
    if (profile == 0) {
        uefi_diag_status(RIBON_UEFI_DIAG_BOOT_VOLUME, EFI_UNSUPPORTED);
        return EFI_UNSUPPORTED;
    }
    gop_required = ribon_profile_has_expectation(profile, RIBON_PROFILE_EXPECT_FRAMEBUFFER);

    uefi_diag_stage(RIBON_UEFI_DIAG_BOOT_VOLUME);
    status = open_boot_volume(image_handle, &root);
    if (EFI_ERROR(status)) {
        uefi_diag_status(RIBON_UEFI_DIAG_BOOT_VOLUME, status);
        uefi_line(L"RIBON-UEFI-BOOT-VOLUME-FAIL\r\n", "RIBON-UEFI-BOOT-VOLUME-FAIL\r\n");
        return status;
    }
    uefi_diag_stage(RIBON_UEFI_DIAG_KERNEL_READ);
    status = read_kernel_payload(root, &kernel_payload, &kernel_buffer);
    direct_high_required =
        boot_option_file_exists(root, L"\\ribon-direct-high") ||
        boot_option_file_exists(root, L"ribon-direct-high");
    (void)root->Close(root);
    if (EFI_ERROR(status)) {
        uefi_diag_status(RIBON_UEFI_DIAG_KERNEL_READ, status);
        uefi_line(L"RIBON-UEFI-KERNEL-READ-FAIL\r\n", "RIBON-UEFI-KERNEL-READ-FAIL\r\n");
        return status;
    }
    uefi_line(L"RIBON-UEFI-KERNEL-READ\r\n", "RIBON-UEFI-KERNEL-READ\r\n");

    uefi_diag_stage(RIBON_UEFI_DIAG_INITIAL_MEMORY_MAP);
    status = capture_memory_map(&memory_capture);
    if (EFI_ERROR(status)) {
        uefi_diag_status(RIBON_UEFI_DIAG_INITIAL_MEMORY_MAP, status);
        uefi_line(L"RIBON-UEFI-MEMORY-MAP-FAIL\r\n", "RIBON-UEFI-MEMORY-MAP-FAIL\r\n");
        release_memory_capture(&memory_capture);
        free_pool(kernel_buffer);
        return status;
    }
    uefi_line(L"RIBON-UEFI-MEMORY-MAP\r\n", "RIBON-UEFI-MEMORY-MAP\r\n");

    ribon_boot_environment_init(&environment, RIBON_FIRMWARE_UEFI, ribon_arch_selected());
    environment.memory_map.regions = memory_capture.regions;
    environment.memory_map.region_count = (uint32_t)memory_capture.region_count;
    environment.boot_media.kind = RIBON_BOOT_MEDIA_FILE;
    environment.boot_media.path = "kernel/kernel.elf";
    environment.command_line.text = command_line;
    environment.command_line.length = count_command_line(command_line);
    environment.flags =
        RIBON_BOOT_ENV_HAS_MEMORY_MAP |
        RIBON_BOOT_ENV_HAS_BOOT_MEDIA |
        RIBON_BOOT_ENV_HAS_COMMAND_LINE;
    uefi_diag_stage(RIBON_UEFI_DIAG_PLATFORM_TABLES);
    collect_platform_tables(&environment, &table_diagnostics);
    report_platform_table_diagnostics(&environment, &table_diagnostics);

    uefi_diag_stage(RIBON_UEFI_DIAG_GOP);
    status = collect_framebuffer(&environment);
    if (EFI_ERROR(status)) {
        uefi_diag_status(RIBON_UEFI_DIAG_GOP, status);
        if (gop_required) {
            uefi_line(L"RIBON-UEFI-GOP-FAIL\r\n", "RIBON-UEFI-GOP-FAIL\r\n");
            release_memory_capture(&memory_capture);
            free_pool(kernel_buffer);
            return status;
        }
        uefi_line(
            L"RIBON-UEFI-GOP-OPTIONAL-MISSING\r\n",
            "RIBON-UEFI-GOP-OPTIONAL-MISSING\r\n");
    } else {
        uefi_line(L"RIBON-UEFI-GOP\r\n", "RIBON-UEFI-GOP\r\n");
    }

    status = boot_parus(
        image_handle,
        &environment,
        &kernel_payload,
        &memory_capture,
        profile,
        direct_high_required);
    if (EFI_ERROR(status)) {
        release_memory_capture(&memory_capture);
        free_pool(kernel_buffer);
        uefi_diag_status(RIBON_UEFI_DIAG_RPH1_REBUILD, status);
        uefi_line(L"RIBON-UEFI-PLAN-FAIL\r\n", "RIBON-UEFI-PLAN-FAIL\r\n");
        return status;
    }

    return EFI_LOAD_ERROR;
}
