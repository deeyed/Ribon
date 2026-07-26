#include <Ribon/arch.h>
#include <Ribon/rpi.h>

#define RIBON_RPI_PL011_DR 0x00ull
#define RIBON_RPI_PL011_FR 0x18ull
#define RIBON_RPI_PL011_FR_TXFF (1u << 5)
#define RIBON_RPI_UART_TIMEOUT 1000000u
#define RIBON_RPI_DTB_MAX_SIZE 0x00200000u
#define RIBON_RPI_DTB_INLINE_MAX_SIZE 0x00010000u

#define RIBON_FDT_MAGIC 0xd00dfeedu
#define RIBON_FDT_TOKEN_BEGIN_NODE 1u
#define RIBON_FDT_TOKEN_END_NODE 2u
#define RIBON_FDT_TOKEN_PROP 3u
#define RIBON_FDT_TOKEN_NOP 4u
#define RIBON_FDT_TOKEN_END 9u

static uint32_t rpi_text_length(const char *text) {
    uint32_t length = 0;
    if (text == 0) {
        return 0;
    }
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static const char *rpi_default_command_line(enum RibonRpiBoardKind board) {
    switch (board) {
    case RIBON_RPI_BOARD_RPI5:
        return "firmware=rpi board=rpi5";
    case RIBON_RPI_BOARD_QEMU_VIRT:
        return "firmware=rpi board=qemu-virt";
    case RIBON_RPI_BOARD_UNKNOWN:
    default:
        return "firmware=rpi board=unknown";
    }
}

static uint32_t rpi_read_be32(const void *address) {
    const unsigned char *bytes = (const unsigned char *)address;
    return ((uint32_t)bytes[0] << 24u) |
           ((uint32_t)bytes[1] << 16u) |
           ((uint32_t)bytes[2] << 8u) |
           (uint32_t)bytes[3];
}

static uint64_t rpi_align4(uint64_t value) {
    return (value + 3u) & ~3ull;
}

static int rpi_bounded_string_equals(const char *text, uint64_t capacity, const char *expected) {
    uint64_t index = 0;
    if (text == 0 || expected == 0) {
        return 0;
    }
    while (index < capacity && expected[index] != '\0' && text[index] == expected[index]) {
        ++index;
    }
    return index < capacity && text[index] == '\0' && expected[index] == '\0';
}

static int rpi_dtb_probe(
    uint64_t physical_address,
    struct RibonDeviceTree *device_tree,
    struct RibonCommandLine *bootargs) {
    const unsigned char *base;
    uint32_t total_size;
    uint32_t struct_offset;
    uint32_t strings_offset;
    uint32_t strings_size;
    uint32_t struct_size;
    uint64_t cursor;
    uint64_t struct_end;
    int depth = 0;
    int chosen_depth = -1;

    if (physical_address == 0u || device_tree == 0 || bootargs == 0) {
        return 0;
    }
    base = (const unsigned char *)(uintptr_t)physical_address;
    if (rpi_read_be32(base) != RIBON_FDT_MAGIC) {
        return 0;
    }
    total_size = rpi_read_be32(base + 4u);
    struct_offset = rpi_read_be32(base + 8u);
    strings_offset = rpi_read_be32(base + 12u);
    strings_size = rpi_read_be32(base + 32u);
    struct_size = rpi_read_be32(base + 36u);
    if (total_size < 40u || total_size > RIBON_RPI_DTB_MAX_SIZE ||
        struct_offset >= total_size || strings_offset >= total_size ||
        strings_size > total_size - strings_offset ||
        struct_size > total_size - struct_offset) {
        return 0;
    }
    device_tree->physical_address = physical_address;
    device_tree->size = total_size;
    device_tree->data = base;

    cursor = struct_offset;
    struct_end = struct_offset + struct_size;
    while (cursor + 4u <= struct_end) {
        const uint32_t token = rpi_read_be32(base + cursor);
        cursor += 4u;
        if (token == RIBON_FDT_TOKEN_BEGIN_NODE) {
            const char *name = (const char *)(base + cursor);
            uint64_t name_capacity = struct_end - cursor;
            uint64_t name_length = 0;
            while (name_length < name_capacity && name[name_length] != '\0') {
                ++name_length;
            }
            if (name_length >= name_capacity) {
                return 1;
            }
            if (depth == 1 && rpi_bounded_string_equals(name, name_capacity, "chosen")) {
                chosen_depth = depth + 1;
            }
            cursor = rpi_align4(cursor + name_length + 1u);
            ++depth;
        } else if (token == RIBON_FDT_TOKEN_END_NODE) {
            if (depth == chosen_depth) {
                chosen_depth = -1;
            }
            if (depth > 0) {
                --depth;
            }
        } else if (token == RIBON_FDT_TOKEN_PROP) {
            uint32_t length;
            uint32_t name_offset;
            const char *property_name;
            const char *property_data;
            if (cursor + 8u > struct_end) {
                return 1;
            }
            length = rpi_read_be32(base + cursor);
            name_offset = rpi_read_be32(base + cursor + 4u);
            cursor += 8u;
            if (length > struct_end - cursor) {
                return 1;
            }
            if (name_offset < strings_size) {
                property_name = (const char *)(base + strings_offset + name_offset);
                property_data = (const char *)(base + cursor);
                if (chosen_depth == depth &&
                    rpi_bounded_string_equals(
                        property_name,
                        strings_size - name_offset,
                        "bootargs") &&
                    length > 0u &&
                    property_data[length - 1u] == '\0') {
                    bootargs->text = property_data;
                    bootargs->length = length - 1u;
                }
            }
            cursor = rpi_align4(cursor + length);
        } else if (token == RIBON_FDT_TOKEN_NOP) {
            continue;
        } else if (token == RIBON_FDT_TOKEN_END) {
            break;
        } else {
            return 1;
        }
    }
    if (device_tree->size > RIBON_RPI_DTB_INLINE_MAX_SIZE) {
        device_tree->data = 0;
    }
    return 1;
}

static uint32_t rpi_mmio_read32(uint64_t address) {
    return *(const volatile uint32_t *)(uintptr_t)address;
}

static void rpi_mmio_write32(uint64_t address, uint32_t value) {
    *(volatile uint32_t *)(uintptr_t)address = value;
}

const char *ribon_rpi_board_name(enum RibonRpiBoardKind board) {
    switch (board) {
    case RIBON_RPI_BOARD_RPI5:
        return "rpi5";
    case RIBON_RPI_BOARD_QEMU_VIRT:
        return "qemu-virt";
    case RIBON_RPI_BOARD_UNKNOWN:
    default:
        return "unknown";
    }
}

const char *ribon_rpi_diagnostic_name(enum RibonRpiDiagnosticCode code) {
    switch (code) {
    case RIBON_RPI_DIAG_OK:
        return "ok";
    case RIBON_RPI_DIAG_ENVIRONMENT:
        return "environment";
    case RIBON_RPI_DIAG_BOOT_MEDIA:
        return "boot-media";
    case RIBON_RPI_DIAG_ELF_LOADER:
        return "elf-loader";
    case RIBON_RPI_DIAG_MEMORY_MAP:
        return "memory-map";
    case RIBON_RPI_DIAG_BOOT_PLAN:
        return "boot-plan";
    case RIBON_RPI_DIAG_SEGMENT_COPY:
        return "segment-copy";
    case RIBON_RPI_DIAG_DEVICE_TREE:
        return "device-tree";
    case RIBON_RPI_DIAG_COMMAND_LINE:
        return "command-line";
    default:
        return "unknown";
    }
}

void ribon_rpi_boot_context_init(
    struct RibonRpiBootContext *context,
    enum RibonRpiBoardKind board,
    uint64_t uart_base,
    uint64_t x0,
    uint64_t x1,
    uint64_t x2,
    uint64_t x3,
    uint64_t current_el,
    uint64_t initial_sp) {
    const char *command_line;
    if (context == 0) {
        return;
    }
    command_line = rpi_default_command_line(board);
    context->board = board;
    context->uart_base = uart_base;
    context->registers.x0 = x0;
    context->registers.x1 = x1;
    context->registers.x2 = x2;
    context->registers.x3 = x3;
    context->registers.current_el = current_el;
    context->registers.initial_sp = initial_sp;
    context->flags = 0;
    context->device_tree.physical_address = x0;
    context->device_tree.size = 0;
    context->device_tree.data = 0;
    context->command_line.text = command_line;
    context->command_line.length = rpi_text_length(command_line);
    if (rpi_dtb_probe(x0, &context->device_tree, &context->command_line) != 0) {
        context->flags |= RIBON_RPI_BOOT_CONTEXT_HAS_VALID_DTB;
        if (context->command_line.text != command_line) {
            context->flags |= RIBON_RPI_BOOT_CONTEXT_HAS_DTB_BOOTARGS;
        }
    }
}

int ribon_rpi_boot_environment_from_context(
    const struct RibonRpiBootContext *context,
    const struct RibonArchDescriptor *arch,
    struct RibonBootEnvironment *out) {
    if (context == 0 || arch == 0 || out == 0) {
        return RIBON_FIRMWARE_STATUS_BAD_ARGUMENT;
    }
    if (!ribon_arch_has_firmware_mask(arch, RIBON_ARCH_FIRMWARE_RASPBERRY_PI)) {
        return RIBON_FIRMWARE_STATUS_UNSUPPORTED;
    }
    ribon_boot_environment_init(out, RIBON_FIRMWARE_RASPBERRY_PI, arch);
    out->device_tree = context->device_tree;
    out->boot_media.kind = RIBON_BOOT_MEDIA_BLOCK;
    out->boot_media.path = "fat:/";
    out->command_line = context->command_line;
    out->flags = RIBON_BOOT_ENV_HAS_BOOT_MEDIA | RIBON_BOOT_ENV_HAS_COMMAND_LINE;
    if ((context->flags & RIBON_RPI_BOOT_CONTEXT_HAS_VALID_DTB) != 0u &&
        context->device_tree.physical_address != 0u &&
        context->device_tree.size != 0u) {
        out->flags |= RIBON_BOOT_ENV_HAS_DEVICE_TREE;
    }
    return RIBON_FIRMWARE_STATUS_OK;
}

void ribon_rpi_uart_write_byte(uint64_t uart_base, char value) {
    if (uart_base == 0u) {
        return;
    }
    for (uint32_t spin = 0; spin < RIBON_RPI_UART_TIMEOUT; ++spin) {
        if ((rpi_mmio_read32(uart_base + RIBON_RPI_PL011_FR) & RIBON_RPI_PL011_FR_TXFF) == 0u) {
            break;
        }
    }
    rpi_mmio_write32(uart_base + RIBON_RPI_PL011_DR, (uint32_t)(uint8_t)value);
}

void ribon_rpi_uart_write_string(uint64_t uart_base, const char *text) {
    if (text == 0) {
        return;
    }
    while (*text != '\0') {
        ribon_rpi_uart_write_byte(uart_base, *text);
        ++text;
    }
}

void ribon_rpi_uart_write_hex64(uint64_t uart_base, uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    for (int shift = 60; shift >= 0; shift -= 4) {
        ribon_rpi_uart_write_byte(uart_base, digits[(value >> (uint32_t)shift) & 0xfull]);
    }
}

static int rpi_probe(const struct RibonArchDescriptor *arch) {
    return ribon_firmware_adapter_supports_arch(ribon_firmware_rpi_adapter(), arch);
}

static int rpi_collect(const struct RibonArchDescriptor *arch, struct RibonBootEnvironment *out) {
    if (arch == 0 || out == 0) {
        return RIBON_FIRMWARE_STATUS_BAD_ARGUMENT;
    }
    ribon_boot_environment_init(out, RIBON_FIRMWARE_RASPBERRY_PI, arch);
    out->boot_media.kind = RIBON_BOOT_MEDIA_BLOCK;
    out->boot_media.path = "fat:/";
    out->command_line.text = "firmware=rpi";
    out->command_line.length = rpi_text_length(out->command_line.text);
    out->flags = RIBON_BOOT_ENV_HAS_BOOT_MEDIA | RIBON_BOOT_ENV_HAS_COMMAND_LINE;
    return RIBON_FIRMWARE_STATUS_OK;
}

static const struct RibonFirmwareAdapter rpi_adapter = {
    .firmware = RIBON_FIRMWARE_RASPBERRY_PI,
    .name = "rpi",
    .probe = rpi_probe,
    .collect = rpi_collect,
};

const struct RibonFirmwareAdapter *ribon_firmware_rpi_adapter(void) {
    return &rpi_adapter;
}
