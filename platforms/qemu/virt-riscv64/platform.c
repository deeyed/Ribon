#include <Ribon/platform/diagnostic.h>
#include <Ribon/platform/facts.h>
#include <Ribon/plugin/descriptor.h>

#define RIBON_QEMU_VIRT_RISCV64_UART_BASE 0x10000000ull
#define RIBON_QEMU_VIRT_RISCV64_UART_THR 0u
#define RIBON_QEMU_VIRT_RISCV64_UART_LSR 5u
#define RIBON_QEMU_VIRT_RISCV64_UART_THR_EMPTY 0x20u

static const struct RibonPlatformFacts selected_platform = {
    .size = sizeof(selected_platform),
    .abi_version = RIBON_PLATFORM_FACTS_ABI_VERSION,
    .id = "virt-riscv64",
    .architecture = RIBON_ARCHITECTURE_RISCV64,
    .environment = RIBON_ENVIRONMENT_RAW_FDT,
    .diagnostic_uart_base = RIBON_QEMU_VIRT_RISCV64_UART_BASE,
    .diagnostic_poll_limit = 1000000u,
    .timer_frequency_hz = 10000000u,
    .native_input_capacity = 2ull * 1024ull * 1024ull,
    .payload_load_base = 0x80400000ull,
    .payload_load_size = 32ull * 1024ull * 1024ull,
};

static volatile uint8_t *selected_uart;
static uint32_t selected_poll_limit;

int ribon_platform_diagnostic_initialize(
    const struct RibonPlatformFacts *facts) {
    if (facts != &selected_platform ||
        facts->diagnostic_uart_base == 0u ||
        facts->diagnostic_poll_limit == 0u) {
        return -1;
    }
    selected_uart =
        (volatile uint8_t *)(uintptr_t)facts->diagnostic_uart_base;
    selected_poll_limit = facts->diagnostic_poll_limit;
    return 0;
}

int ribon_platform_diagnostic_write(const char *text) {
    if (selected_uart == 0 || text == 0 || selected_poll_limit == 0u) {
        return -1;
    }
    while (*text != '\0') {
        uint32_t poll = 0u;
        while ((selected_uart[RIBON_QEMU_VIRT_RISCV64_UART_LSR] &
                RIBON_QEMU_VIRT_RISCV64_UART_THR_EMPTY) == 0u &&
               poll < selected_poll_limit) {
            ++poll;
        }
        if (poll == selected_poll_limit) {
            return -2;
        }
        selected_uart[RIBON_QEMU_VIRT_RISCV64_UART_THR] = (uint8_t)*text;
        ++text;
    }
    return 0;
}

const struct RibonPlatformFacts *ribon_platform_selected(void) {
    return &selected_platform;
}

const struct RibonPluginDescriptor ribon_platform_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_platform_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_PLATFORM,
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION,
    .id = "platform.virt-riscv64",
    .provides = RIBON_CAP_PLATFORM_FACTS,
    .requires = RIBON_CAP_ARCHITECTURE,
    .architecture_mask = RIBON_ARCH_MASK_RISCV64,
    .environment_mask = RIBON_ENV_MASK_RAW_FDT,
    .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 4096u,
    .input_budget = 2ull * 1024ull * 1024ull,
    .output_budget = 4096u,
    .deadline_ms = 30000u,
    .operations = &selected_platform,
    .operations_size = sizeof(selected_platform),
    .operations_abi = RIBON_PLATFORM_FACTS_ABI_VERSION,
    .validate_operations = ribon_platform_plugin_operations_are_valid,
};
