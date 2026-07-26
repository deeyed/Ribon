#ifndef RIBON_RPI_H
#define RIBON_RPI_H

#include <stdint.h>

#include <Ribon/firmware.h>

enum RibonRpiBoardKind {
    RIBON_RPI_BOARD_UNKNOWN = 0,
    RIBON_RPI_BOARD_RPI5 = 1,
    RIBON_RPI_BOARD_QEMU_VIRT = 2,
};

enum RibonRpiDiagnosticCode {
    RIBON_RPI_DIAG_OK = 0x00,
    RIBON_RPI_DIAG_ENVIRONMENT = 0x10,
    RIBON_RPI_DIAG_BOOT_MEDIA = 0x20,
    RIBON_RPI_DIAG_ELF_LOADER = 0x30,
    RIBON_RPI_DIAG_MEMORY_MAP = 0x40,
    RIBON_RPI_DIAG_BOOT_PLAN = 0x50,
    RIBON_RPI_DIAG_SEGMENT_COPY = 0x60,
    RIBON_RPI_DIAG_DEVICE_TREE = 0x70,
    RIBON_RPI_DIAG_COMMAND_LINE = 0x80,
};

enum RibonRpiBootContextFlags {
    RIBON_RPI_BOOT_CONTEXT_HAS_VALID_DTB = 1u << 0,
    RIBON_RPI_BOOT_CONTEXT_HAS_DTB_BOOTARGS = 1u << 1,
    RIBON_RPI_BOOT_CONTEXT_HAS_BOOT_MEDIA_CMDLINE = 1u << 2,
};

struct RibonRpiInitialRegisters {
    uint64_t x0;
    uint64_t x1;
    uint64_t x2;
    uint64_t x3;
    uint64_t current_el;
    uint64_t initial_sp;
};

struct RibonRpiBootContext {
    enum RibonRpiBoardKind board;
    uint64_t uart_base;
    struct RibonRpiInitialRegisters registers;
    struct RibonDeviceTree device_tree;
    struct RibonCommandLine command_line;
    uint32_t flags;
};

struct RibonRpiHandoffRegisters {
    uint64_t x0_rph1;
    uint64_t x1_entry_flags;
    uint64_t entry_point;
};

const char *ribon_rpi_board_name(enum RibonRpiBoardKind board);
const char *ribon_rpi_diagnostic_name(enum RibonRpiDiagnosticCode code);
void ribon_rpi_boot_context_init(
    struct RibonRpiBootContext *context,
    enum RibonRpiBoardKind board,
    uint64_t uart_base,
    uint64_t x0,
    uint64_t x1,
    uint64_t x2,
    uint64_t x3,
    uint64_t current_el,
    uint64_t initial_sp);
int ribon_rpi_boot_environment_from_context(
    const struct RibonRpiBootContext *context,
    const struct RibonArchDescriptor *arch,
    struct RibonBootEnvironment *out);
void ribon_rpi_uart_write_byte(uint64_t uart_base, char value);
void ribon_rpi_uart_write_string(uint64_t uart_base, const char *text);
void ribon_rpi_uart_write_hex64(uint64_t uart_base, uint64_t value);
void ribon_rpi_prepare_handoff_state(void);
void ribon_rpi_enter_kernel(uint64_t entry, uint64_t rph1, uint64_t entry_flags);

#endif
