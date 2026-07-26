#include <Ribon/memory.h>
#include <Ribon/profiles/parus/rph1.h>
#include <Ribon/ribon.h>
#include <Ribon/rpi.h>

#include <string.h>

#ifndef RIBON_RPI_UART_BASE
#define RIBON_RPI_UART_BASE 0x09000000ull
#endif

#ifndef RIBON_RPI_BOARD_KIND
#define RIBON_RPI_BOARD_KIND RIBON_RPI_BOARD_QEMU_VIRT
#endif

#ifndef RIBON_RPI_PLATFORM_NAME
#define RIBON_RPI_PLATFORM_NAME "qemu-virt"
#endif

#ifndef RIBON_RPI_RAM_BASE
#define RIBON_RPI_RAM_BASE 0x00000000ull
#endif

#ifndef RIBON_RPI_RAM_SIZE
#define RIBON_RPI_RAM_SIZE 0x10000000ull
#endif

#ifndef RIBON_RPI_ENABLE_KERNEL_JUMP
#define RIBON_RPI_ENABLE_KERNEL_JUMP 0
#endif

#define RIBON_RPI_MAX_MEMORY_REGIONS 8u
#define RIBON_RPI_MAX_LOAD_SEGMENTS 8u
#define RIBON_RPI_RPH1_BUFFER_SIZE RIBON_PARUS_RPH1_MAX_TOTAL_SIZE
#define RIBON_RPI_RESERVED_LOW_SIZE 0x00080000ull
#define RIBON_RPI_CMDLINE_BUFFER_SIZE 512u
#define RIBON_RPI_BOOT_KERNEL_PATH "kernel/kernel.elf"
#define RIBON_RPI_BOOT_CMDLINE_PATH "cmdline.txt"
#define RIBON_RPI_BOOT_CONFIG_PATH "config.txt"

extern const unsigned char __ribon_rpi_boot_media_parus_elf_start[];
extern const unsigned char __ribon_rpi_boot_media_parus_elf_end[];
extern const unsigned char __ribon_rpi_boot_media_cmdline_start[];
extern const unsigned char __ribon_rpi_boot_media_cmdline_end[];
extern char __image_start;
extern char __image_end;

static struct RibonMemoryRegion g_memory_regions[RIBON_RPI_MAX_MEMORY_REGIONS];
static struct RibonMemoryRegion g_normalized_regions[RIBON_RPI_MAX_MEMORY_REGIONS];
static struct RibonLoadSegment g_kernel_segments[RIBON_RPI_MAX_LOAD_SEGMENTS];
static unsigned char g_rph1_buffer[RIBON_RPI_RPH1_BUFFER_SIZE] __attribute__((aligned(4096)));
static char g_cmdline_buffer[RIBON_RPI_CMDLINE_BUFFER_SIZE];

static void rpi_log_line(uint64_t uart_base, const char *text) {
    ribon_rpi_uart_write_string(uart_base, text);
    ribon_rpi_uart_write_string(uart_base, "\r\n");
}

static void rpi_log_hex_line(uint64_t uart_base, const char *label, uint64_t value) {
    ribon_rpi_uart_write_string(uart_base, label);
    ribon_rpi_uart_write_hex64(uart_base, value);
    ribon_rpi_uart_write_string(uart_base, "\r\n");
}

static void rpi_log_diag(uint64_t uart_base, enum RibonRpiDiagnosticCode code) {
    rpi_log_hex_line(uart_base, "RIBON-RPI-DIAG=0x", (uint64_t)code);
    ribon_rpi_uart_write_string(uart_base, "RIBON-RPI-DIAG-TEXT=");
    rpi_log_line(uart_base, ribon_rpi_diagnostic_name(code));
}

static uint64_t rpi_align_up(uint64_t value, uint64_t alignment) {
    if (alignment == 0u) {
        return value;
    }
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static uint64_t rpi_align_down(uint64_t value, uint64_t alignment) {
    if (alignment == 0u) {
        return value;
    }
    return value & ~(alignment - 1u);
}

static uint64_t rpi_boot_media_file_size(const unsigned char *start, const unsigned char *end) {
    if (start == 0 || end == 0 || end < start) {
        return 0;
    }
    return (uint64_t)(end - start);
}

static uint64_t rpi_trimmed_text_size(const unsigned char *data, uint64_t size) {
    while (size != 0u && (data[size - 1u] == '\n' || data[size - 1u] == '\r' || data[size - 1u] == '\0')) {
        --size;
    }
    return size;
}

static int rpi_boot_media_kernel_payload(struct RibonPayloadImage *out) {
    const uint64_t size = rpi_boot_media_file_size(
        __ribon_rpi_boot_media_parus_elf_start,
        __ribon_rpi_boot_media_parus_elf_end);
    if (out == 0 || size == 0u) {
        return -1;
    }
    out->data = __ribon_rpi_boot_media_parus_elf_start;
    out->size = size;
    out->source_name = "boot-media:kernel/kernel.elf";
    return 0;
}

static int rpi_boot_media_apply_cmdline(struct RibonRpiBootContext *context) {
    const unsigned char *start = __ribon_rpi_boot_media_cmdline_start;
    const uint64_t raw_size =
        rpi_boot_media_file_size(start, __ribon_rpi_boot_media_cmdline_end);
    const uint64_t size = rpi_trimmed_text_size(start, raw_size);
    if (context == 0) {
        return -1;
    }
    if ((context->flags & RIBON_RPI_BOOT_CONTEXT_HAS_DTB_BOOTARGS) != 0u || size == 0u) {
        return 0;
    }
    if (size >= RIBON_RPI_CMDLINE_BUFFER_SIZE) {
        return -1;
    }
    for (uint64_t index = 0; index < size; ++index) {
        g_cmdline_buffer[index] = (char)start[index];
    }
    g_cmdline_buffer[size] = '\0';
    context->command_line.text = g_cmdline_buffer;
    context->command_line.length = (uint32_t)size;
    context->flags |= RIBON_RPI_BOOT_CONTEXT_HAS_BOOT_MEDIA_CMDLINE;
    return 0;
}

static int rpi_add_region(
    struct RibonMemoryRegion *regions,
    uint32_t *count,
    uint64_t base,
    uint64_t length,
    enum RibonMemoryRegionKind kind,
    uint64_t attributes) {
    if (regions == 0 || count == 0 || length == 0u || *count >= RIBON_RPI_MAX_MEMORY_REGIONS) {
        return -1;
    }
    regions[*count].base = base;
    regions[*count].length = length;
    regions[*count].kind = kind;
    regions[*count].attributes = attributes;
    ++(*count);
    return 0;
}

static int rpi_add_usable_range_with_dtb(
    struct RibonMemoryRegion *regions,
    uint32_t *count,
    uint64_t start,
    uint64_t end,
    uint64_t dtb_base,
    uint64_t dtb_end) {
    uint64_t reserved_start;
    uint64_t reserved_end;
    if (end <= start) {
        return 0;
    }
    if (dtb_base == 0u || dtb_end <= dtb_base || dtb_end <= start || dtb_base >= end) {
        return rpi_add_region(
            regions,
            count,
            start,
            end - start,
            RIBON_MEMORY_REGION_USABLE,
            RIBON_MEMORY_ATTR_READ | RIBON_MEMORY_ATTR_WRITE | RIBON_MEMORY_ATTR_EXECUTE);
    }
    reserved_start = dtb_base > start ? dtb_base : start;
    reserved_end = dtb_end < end ? dtb_end : end;
    if (start < reserved_start &&
        rpi_add_region(
            regions,
            count,
            start,
            reserved_start - start,
            RIBON_MEMORY_REGION_USABLE,
            RIBON_MEMORY_ATTR_READ | RIBON_MEMORY_ATTR_WRITE | RIBON_MEMORY_ATTR_EXECUTE) != 0) {
        return -1;
    }
    if (rpi_add_region(
            regions,
            count,
            reserved_start,
            reserved_end - reserved_start,
            RIBON_MEMORY_REGION_FIRMWARE,
            RIBON_MEMORY_ATTR_READ | RIBON_MEMORY_ATTR_BOOT_RECLAIMABLE) != 0) {
        return -1;
    }
    if (reserved_end < end &&
        rpi_add_region(
            regions,
            count,
            reserved_end,
            end - reserved_end,
            RIBON_MEMORY_REGION_USABLE,
            RIBON_MEMORY_ATTR_READ | RIBON_MEMORY_ATTR_WRITE | RIBON_MEMORY_ATTR_EXECUTE) != 0) {
        return -1;
    }
    return 0;
}

static int rpi_build_prototype_memory_map(
    struct RibonBootEnvironment *environment,
    const struct RibonLoadedPayload *kernel_layout) {
    const uint64_t ram_base = RIBON_RPI_RAM_BASE;
    const uint64_t ram_end = RIBON_RPI_RAM_BASE + RIBON_RPI_RAM_SIZE;
    const uint64_t reserved_low_end = RIBON_RPI_RAM_BASE + RIBON_RPI_RESERVED_LOW_SIZE;
    const uint64_t image_start = (uint64_t)(uintptr_t)&__image_start;
    const uint64_t image_end = rpi_align_up((uint64_t)(uintptr_t)&__image_end, 4096u);
    const uint64_t kernel_base = kernel_layout->load_base;
    const uint64_t kernel_end = rpi_align_up(kernel_layout->load_end, 4096u);
    uint64_t dtb_base = 0;
    uint64_t dtb_end = 0;
    uint32_t count = 0;

    if (environment == 0 || kernel_layout == 0 || kernel_layout->segment_count == 0u ||
        ram_end <= ram_base || reserved_low_end > ram_end || image_start < reserved_low_end ||
        image_end > kernel_base || kernel_base < ram_base || kernel_end > ram_end) {
        return -1;
    }
    if ((environment->flags & RIBON_BOOT_ENV_HAS_DEVICE_TREE) != 0u &&
        environment->device_tree.physical_address >= ram_base &&
        environment->device_tree.size <= UINT64_MAX - environment->device_tree.physical_address &&
        environment->device_tree.physical_address + environment->device_tree.size <= UINT64_MAX - 4095u) {
        dtb_base = rpi_align_down(environment->device_tree.physical_address, 4096u);
        dtb_end = rpi_align_up(environment->device_tree.physical_address + environment->device_tree.size, 4096u);
        if (dtb_end > ram_end) {
            dtb_base = 0;
            dtb_end = 0;
        }
    }
    if (rpi_add_region(
            g_memory_regions,
            &count,
            ram_base,
            reserved_low_end - ram_base,
            RIBON_MEMORY_REGION_RESERVED,
            RIBON_MEMORY_ATTR_READ | RIBON_MEMORY_ATTR_WRITE) != 0) {
        return -1;
    }
    if (rpi_add_region(
            g_memory_regions,
            &count,
            image_start,
            image_end - image_start,
            RIBON_MEMORY_REGION_BOOTLOADER,
            RIBON_MEMORY_ATTR_READ |
                RIBON_MEMORY_ATTR_WRITE |
                RIBON_MEMORY_ATTR_EXECUTE |
                RIBON_MEMORY_ATTR_BOOT_RECLAIMABLE) != 0) {
        return -1;
    }
    if (rpi_add_usable_range_with_dtb(
            g_memory_regions,
            &count,
            image_end,
            kernel_base,
            dtb_base,
            dtb_end) != 0) {
        return -1;
    }
    if (rpi_add_region(
            g_memory_regions,
            &count,
            kernel_base,
            kernel_end - kernel_base,
            RIBON_MEMORY_REGION_KERNEL_IMAGE,
            RIBON_MEMORY_ATTR_READ | RIBON_MEMORY_ATTR_WRITE | RIBON_MEMORY_ATTR_EXECUTE) != 0) {
        return -1;
    }
    if (rpi_add_usable_range_with_dtb(
            g_memory_regions,
            &count,
            kernel_end,
            ram_end,
            dtb_base,
            dtb_end) != 0) {
        return -1;
    }

    environment->memory_map.regions = g_memory_regions;
    environment->memory_map.region_count = count;
    environment->flags |= RIBON_BOOT_ENV_HAS_MEMORY_MAP;
    return 0;
}

static int rpi_copy_kernel_segments(
    const struct RibonPayloadImage *payload,
    const struct RibonLoadedPayload *layout) {
    if (payload == 0 || payload->data == 0 || layout == 0) {
        return -1;
    }
    for (uint32_t index = 0; index < layout->segment_count; ++index) {
        const struct RibonLoadSegment *segment = &layout->segments[index];
        const unsigned char *source = (const unsigned char *)payload->data + segment->file_offset;
        unsigned char *destination = (unsigned char *)(uintptr_t)segment->load_address;
        if (segment->file_offset > payload->size || segment->file_size > payload->size - segment->file_offset ||
            segment->file_size > segment->memory_size) {
            return -1;
        }
        for (uint64_t offset = 0; offset < segment->file_size; ++offset) {
            destination[offset] = source[offset];
        }
        for (uint64_t offset = segment->file_size; offset < segment->memory_size; ++offset) {
            destination[offset] = 0u;
        }
    }
    return 0;
}

/* The QEMU/RPi smoke path is still pre-MMU; keep this aggregate fill conservative. */
#if defined(__clang__)
__attribute__((noinline, optnone))
#endif
static void rpi_fill_parus_plan(
    const struct RibonBootEnvironment *environment,
    const struct RibonProfile *profile,
    const struct RibonMutableMemoryMap *normalized_memory_map,
    const struct RibonPayloadImage *kernel_payload,
    const struct RibonLoadedPayload *kernel_layout,
    struct RibonBootPlan *plan_out) {
    memset(plan_out, 0, sizeof(*plan_out));
    plan_out->firmware = environment->firmware;
    plan_out->arch = environment->arch;
    plan_out->environment_flags = environment->flags;
    plan_out->memory_region_count = environment->memory_map.region_count;
    plan_out->normalized_memory_region_count = normalized_memory_map->region_count;
    plan_out->usable_memory_bytes =
        ribon_memory_map_usable_bytes(
            &(const struct RibonMemoryMap){
                .regions = normalized_memory_map->regions,
                .region_count = normalized_memory_map->region_count,
            });
    plan_out->boot_media = environment->boot_media.kind;
    plan_out->boot_module_count = environment->boot_modules.module_count;
    plan_out->device_tree_address = environment->device_tree.physical_address;
    plan_out->device_tree_size = environment->device_tree.size;
    plan_out->framebuffer_address = environment->framebuffer.physical_address;
    plan_out->command_line = environment->command_line.text;
    plan_out->profile_name = profile->name;
    plan_out->kernel_path = profile->kernel_path;
    plan_out->kernel_source_name = kernel_payload->source_name;
    plan_out->handoff_name =
        profile->handoff_name != 0 ? profile->handoff_name : ribon_handoff_name(profile->handoff);
    plan_out->kernel_format = kernel_layout->format;
    plan_out->kernel_machine = kernel_layout->machine;
    plan_out->kernel_load_segment_count = kernel_layout->segment_count;
    plan_out->kernel_entry_point = kernel_layout->entry_point;
    plan_out->kernel_entry_load_address = kernel_layout->entry_load_address;
    plan_out->kernel_runtime_entry_address = kernel_layout->runtime_entry_address;
    plan_out->kernel_load_base = kernel_layout->load_base;
    plan_out->kernel_load_end = kernel_layout->load_end;
    plan_out->kernel_runtime_load_base = kernel_layout->runtime_load_base;
    plan_out->kernel_runtime_load_end = kernel_layout->runtime_load_end;
    plan_out->kernel_memory_size = kernel_layout->memory_size;
    plan_out->kernel_linked_virtual_base = kernel_layout->linked_virtual_base;
    plan_out->kernel_linked_virtual_end = kernel_layout->linked_virtual_end;
    plan_out->kernel_linked_physical_base = kernel_layout->linked_physical_base;
    plan_out->kernel_linked_physical_end = kernel_layout->linked_physical_end;
    plan_out->kernel_high_entry_virtual_address = kernel_layout->high_entry_virtual_address;
    plan_out->kernel_high_entry_load_address = kernel_layout->high_entry_load_address;
    plan_out->kernel_load_segments = kernel_layout->segments;
    plan_out->kernel_load_plan_flags = kernel_layout->load_plan_flags;
    plan_out->expectations = profile->expectations;
    plan_out->handoff = profile->handoff;
    plan_out->handoff_major = profile->handoff_major;
}

static int rpi_build_handoff(
    uint64_t uart_base,
    const struct RibonRpiBootContext *context,
    struct RibonBootEnvironment *environment,
    const struct RibonPayloadImage *kernel_payload,
    struct RibonRpiHandoffRegisters *registers_out,
    struct RibonBootPlan *plan_out) {
    struct RibonLoadedPayload preliminary_layout = {
        .segments = g_kernel_segments,
        .segment_capacity = RIBON_RPI_MAX_LOAD_SEGMENTS,
    };
    struct RibonMutableMemoryMap normalized_memory_map = {
        .regions = g_normalized_regions,
        .region_count = 0,
        .capacity = RIBON_RPI_MAX_MEMORY_REGIONS,
    };
    struct RibonLoadedPayload kernel_layout = {
        .segments = g_kernel_segments,
        .segment_capacity = RIBON_RPI_MAX_LOAD_SEGMENTS,
    };
    struct RibonHandoffArtifact handoff_artifact = {0};
    const struct RibonProfile *profile = ribon_find_builtin_profile("parus");
    int loader_status;
    int memory_status;
    int handoff_status;

    if (context == 0 || environment == 0 || kernel_payload == 0 || registers_out == 0 ||
        plan_out == 0 || profile == 0) {
        return RIBON_RPI_DIAG_ENVIRONMENT;
    }
    if (kernel_payload->data == 0 || kernel_payload->size == 0u) {
        return RIBON_RPI_DIAG_BOOT_MEDIA;
    }
    rpi_log_line(uart_base, "RIBON-RPI-LOADER-START");
    loader_status = ribon_loader_analyze(kernel_payload, environment->arch, &preliminary_layout);
    rpi_log_hex_line(uart_base, "RIBON-RPI-LOADER-STATUS=0x", (uint64_t)(int64_t)loader_status);
    if (loader_status != RIBON_LOADER_STATUS_OK) {
        ribon_rpi_uart_write_string(uart_base, "RIBON-RPI-LOADER-TEXT=");
        rpi_log_line(uart_base, ribon_loader_status_name((enum RibonLoaderStatus)loader_status));
        return RIBON_RPI_DIAG_ELF_LOADER;
    }
    rpi_log_hex_line(uart_base, "RIBON-RPI-LOADER-SEGMENTS=0x", preliminary_layout.segment_count);
    rpi_log_hex_line(uart_base, "RIBON-RPI-LOADER-BASE=0x", preliminary_layout.load_base);
    rpi_log_hex_line(uart_base, "RIBON-RPI-LOADER-END=0x", preliminary_layout.load_end);
    if (rpi_build_prototype_memory_map(environment, &preliminary_layout) != 0) {
        return RIBON_RPI_DIAG_MEMORY_MAP;
    }
    rpi_log_line(uart_base, "RIBON-RPI-MEMORY-MAP-OK");

    rpi_log_line(uart_base, "RIBON-RPI-NORMALIZE-START");
    memory_status = ribon_memory_map_normalize(&environment->memory_map, &normalized_memory_map);
    rpi_log_hex_line(uart_base, "RIBON-RPI-NORMALIZE-STATUS=0x", (uint64_t)(int64_t)memory_status);
    if (memory_status != RIBON_MEMORY_STATUS_OK) {
        return RIBON_RPI_DIAG_MEMORY_MAP;
    }
    rpi_log_hex_line(uart_base, "RIBON-RPI-NORMALIZED-REGIONS=0x", normalized_memory_map.region_count);

    rpi_log_line(uart_base, "RIBON-RPI-KERNEL-LAYOUT-START");
    loader_status = ribon_loader_analyze(kernel_payload, environment->arch, &kernel_layout);
    rpi_log_hex_line(uart_base, "RIBON-RPI-KERNEL-LAYOUT-STATUS=0x", (uint64_t)(int64_t)loader_status);
    if (loader_status != RIBON_LOADER_STATUS_OK) {
        return RIBON_RPI_DIAG_ELF_LOADER;
    }

    rpi_log_line(uart_base, "RIBON-RPI-PLAN-FILL-START");
    rpi_fill_parus_plan(
        environment,
        profile,
        &normalized_memory_map,
        kernel_payload,
        &kernel_layout,
        plan_out);
    rpi_log_line(uart_base, "RIBON-RPI-PLAN-FILL-OK");
    rpi_log_line(uart_base, "RIBON-RPI-RPH1-START");
    handoff_status = ribon_parus_build_rph1(
        plan_out,
        environment,
        &normalized_memory_map,
        g_rph1_buffer,
        sizeof(g_rph1_buffer),
        &handoff_artifact);
    rpi_log_hex_line(uart_base, "RIBON-RPI-RPH1-STATUS=0x", (uint64_t)(int64_t)handoff_status);
    if (handoff_status != RIBON_PROFILE_HANDOFF_STATUS_OK) {
        return RIBON_RPI_DIAG_BOOT_PLAN;
    }
    plan_out->handoff_artifact_format = handoff_artifact.format;
    plan_out->handoff_artifact_size = handoff_artifact.size;
    plan_out->handoff_artifact_sections = handoff_artifact.section_count;
    rpi_log_line(uart_base, "RIBON-RPI-BOOT-PLAN-OK");
    rpi_log_line(uart_base, "RIBON-RPI-SEGMENT-COPY-START");
    if (rpi_copy_kernel_segments(kernel_payload, &kernel_layout) != 0) {
        return RIBON_RPI_DIAG_SEGMENT_COPY;
    }

    registers_out->x0_rph1 = (uint64_t)(uintptr_t)handoff_artifact.data;
    registers_out->x1_entry_flags = RIBON_KERNEL_ENTRY_FLAG_RPH1;
    registers_out->entry_point = plan_out->kernel_entry_point;
    return RIBON_RPI_DIAG_OK;
}

void ribon_rpi_payload_entry(
    uint64_t x0,
    uint64_t x1,
    uint64_t x2,
    uint64_t x3,
    uint64_t current_el,
    uint64_t initial_sp) {
    const uint64_t uart_base = (uint64_t)RIBON_RPI_UART_BASE;
    struct RibonRpiBootContext context;
    struct RibonBootEnvironment environment;
    struct RibonRpiHandoffRegisters handoff_registers = {0};
    struct RibonBootPlan plan = {0};
    struct RibonPayloadImage kernel_payload = {0};
    ribon_rpi_boot_context_init(
        &context,
        RIBON_RPI_BOARD_KIND,
        uart_base,
        x0,
        x1,
        x2,
        x3,
        current_el,
        initial_sp);

    if (rpi_boot_media_kernel_payload(&kernel_payload) != 0) {
        rpi_log_diag(uart_base, RIBON_RPI_DIAG_BOOT_MEDIA);
        rpi_log_line(uart_base, "RIBON-RPI-BOOT-MEDIA-FAIL");
        for (;;) {
            __asm__ volatile("wfe");
        }
    }
    if (rpi_boot_media_apply_cmdline(&context) != 0) {
        rpi_log_diag(uart_base, RIBON_RPI_DIAG_COMMAND_LINE);
        rpi_log_line(uart_base, "RIBON-RPI-CMDLINE-FAIL");
        for (;;) {
            __asm__ volatile("wfe");
        }
    }

    rpi_log_line(uart_base, "RIBON-RPI-START");
    ribon_rpi_uart_write_string(uart_base, "RIBON-RPI-PLATFORM=");
    rpi_log_line(uart_base, RIBON_RPI_PLATFORM_NAME);
    ribon_rpi_uart_write_string(uart_base, "RIBON-RPI-BOARD=");
    rpi_log_line(uart_base, ribon_rpi_board_name(context.board));
    rpi_log_hex_line(uart_base, "RIBON-RPI-DTB=0x", context.device_tree.physical_address);
    rpi_log_hex_line(uart_base, "RIBON-RPI-X0=0x", context.registers.x0);
    rpi_log_hex_line(uart_base, "RIBON-RPI-X1=0x", context.registers.x1);
    rpi_log_hex_line(uart_base, "RIBON-RPI-X2=0x", context.registers.x2);
    rpi_log_hex_line(uart_base, "RIBON-RPI-X3=0x", context.registers.x3);
    rpi_log_hex_line(uart_base, "RIBON-RPI-CURRENT-EL=0x", context.registers.current_el);
    rpi_log_hex_line(uart_base, "RIBON-RPI-INITIAL-SP=0x", context.registers.initial_sp);
    rpi_log_hex_line(uart_base, "RIBON-RPI-DTB-SIZE=0x", context.device_tree.size);
    rpi_log_line(
        uart_base,
        (context.flags & RIBON_RPI_BOOT_CONTEXT_HAS_VALID_DTB) != 0u ?
            "RIBON-RPI-DTB-SOURCE=firmware" :
            "RIBON-RPI-DTB-SOURCE=missing");
    if ((context.flags & RIBON_RPI_BOOT_CONTEXT_HAS_VALID_DTB) != 0u &&
        context.device_tree.size != 0u) {
        rpi_log_line(
            uart_base,
            context.device_tree.data != 0 ?
                "RIBON-RPI-DTB-RPH1=inline" :
                "RIBON-RPI-DTB-RPH1=descriptor");
    }
    if ((context.flags & RIBON_RPI_BOOT_CONTEXT_HAS_DTB_BOOTARGS) != 0u) {
        rpi_log_line(uart_base, "RIBON-RPI-CMDLINE-SOURCE=dtb:/chosen/bootargs");
    } else if ((context.flags & RIBON_RPI_BOOT_CONTEXT_HAS_BOOT_MEDIA_CMDLINE) != 0u) {
        rpi_log_line(uart_base, "RIBON-RPI-CMDLINE-SOURCE=boot-media:cmdline.txt");
    } else {
        rpi_log_line(uart_base, "RIBON-RPI-CMDLINE-SOURCE=default");
    }
    ribon_rpi_uart_write_string(uart_base, "RIBON-RPI-CMDLINE=");
    rpi_log_line(uart_base, context.command_line.text);
    rpi_log_line(uart_base, "RIBON-RPI-HANDOFF-START");
    rpi_log_line(uart_base, "RIBON-RPI-BOOT-MEDIA=package");
    rpi_log_line(uart_base, "RIBON-RPI-BOOT-MEDIA-BACKEND=embedded-package");
    rpi_log_line(uart_base, "RIBON-RPI-BOOT-KERNEL-PATH=" RIBON_RPI_BOOT_KERNEL_PATH);
    rpi_log_line(uart_base, "RIBON-RPI-BOOT-CMDLINE-PATH=" RIBON_RPI_BOOT_CMDLINE_PATH);
    rpi_log_line(uart_base, "RIBON-RPI-BOOT-CONFIG-PATH=" RIBON_RPI_BOOT_CONFIG_PATH);
    rpi_log_hex_line(uart_base, "RIBON-RPI-ELF-SIZE=0x", kernel_payload.size);

    if (ribon_rpi_boot_environment_from_context(&context, ribon_arch_selected(), &environment) ==
        RIBON_FIRMWARE_STATUS_OK) {
        environment.boot_media.path = "fat:/";
        environment.boot_media.size = kernel_payload.size;
        rpi_log_line(uart_base, "RIBON-RPI-ENV-OK");
    } else {
        rpi_log_diag(uart_base, RIBON_RPI_DIAG_ENVIRONMENT);
        rpi_log_line(uart_base, "RIBON-RPI-ENV-FAIL");
        for (;;) {
            __asm__ volatile("wfe");
        }
    }

    const int handoff_status =
        rpi_build_handoff(uart_base, &context, &environment, &kernel_payload, &handoff_registers, &plan);
    if (handoff_status != RIBON_RPI_DIAG_OK) {
        rpi_log_diag(uart_base, (enum RibonRpiDiagnosticCode)handoff_status);
        rpi_log_line(uart_base, "RIBON-RPI-HANDOFF-FAIL");
        for (;;) {
            __asm__ volatile("wfe");
        }
    }

    rpi_log_hex_line(uart_base, "RIBON-RPI-SEGMENT-COUNT=0x", plan.kernel_load_segment_count);
    rpi_log_hex_line(uart_base, "RIBON-RPI-KERNEL-BASE=0x", plan.kernel_load_base);
    rpi_log_hex_line(uart_base, "RIBON-RPI-KERNEL-END=0x", plan.kernel_load_end);
    rpi_log_hex_line(uart_base, "RIBON-RPI-KERNEL-ENTRY=0x", handoff_registers.entry_point);
    rpi_log_line(uart_base, "RIBON-RPI-SEGMENT-COPY-OK");
    rpi_log_hex_line(uart_base, "RIBON-RPI-RPH1=0x", handoff_registers.x0_rph1);
    rpi_log_hex_line(uart_base, "RIBON-RPI-RPH1-SIZE=0x", plan.handoff_artifact_size);
    rpi_log_hex_line(uart_base, "RIBON-RPI-RPH1-SECTIONS=0x", plan.handoff_artifact_sections);
    rpi_log_hex_line(uart_base, "RIBON-RPI-HANDOFF-X0=0x", handoff_registers.x0_rph1);
    rpi_log_hex_line(uart_base, "RIBON-RPI-HANDOFF-X1=0x", handoff_registers.x1_entry_flags);
    rpi_log_line(uart_base, "RIBON-RPI-MEMORY-MAP=prototype");
    rpi_log_line(uart_base, "RIBON-RPI-MMU=unchanged");
    rpi_log_line(uart_base, "RIBON-RPI-CACHE=unchanged");
    ribon_rpi_prepare_handoff_state();
    rpi_log_line(uart_base, "RIBON-RPI-INTERRUPTS=masked");
    rpi_log_line(uart_base, "RIBON-RPI-HANDOFF-READY");
#if RIBON_RPI_ENABLE_KERNEL_JUMP != 0
    ribon_rpi_enter_kernel(
        handoff_registers.entry_point,
        handoff_registers.x0_rph1,
        handoff_registers.x1_entry_flags);
#else
    rpi_log_line(uart_base, "RIBON-RPI-HANDOFF-SMOKE-OK");
    rpi_log_line(uart_base, "RIBON-RPI-ALIVE");
#endif

    for (;;) {
        __asm__ volatile("wfe");
    }
}
