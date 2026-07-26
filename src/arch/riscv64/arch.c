#include <Ribon/arch/entry.h>
#include <Ribon/arch/ops.h>
#include <Ribon/boot/image.h>
#include <Ribon/plugin/descriptor.h>

static const struct RibonArchDescriptor riscv64_arch = {
    .size = sizeof(riscv64_arch),
    .abi_version = RIBON_ARCH_OPS_ABI_VERSION,
    .id = RIBON_ARCHITECTURE_RISCV64,
    .canonical_name = "riscv64",
    .family_name = "riscv",
    .word_bits = 64,
    .physical_address_bits = 56,
    .virtual_address_bits = 39,
    .page_size = 4096,
    .large_page_size = 2097152,
    .kernel_alignment = 2097152,
    .handoff_alignment = 4096,
    .boot_module_alignment = 4096,
    .endian = RIBON_ARCH_ENDIAN_LITTLE,
    .tier = RIBON_ARCH_TIER_FUTURE,
};

/** @brief 선택된 RISC-V 64 architecture descriptor를 반환한다. */
const struct RibonArchDescriptor *ribon_arch_selected(void) {
    return &riscv64_arch;
}

uint64_t ribon_arch_direct_high_page_table_pages(const struct RibonLoadedPayload *payload) {
    (void)payload;
    return 0;
}

int ribon_arch_prepare_direct_high_entry(
    const struct RibonLoadedPayload *payload,
    uint64_t page_table_physical_address,
    void *page_table_buffer,
    uint64_t page_table_size,
    struct RibonArchDirectHighHandoff *out) {
    (void)payload;
    (void)page_table_physical_address;
    (void)page_table_buffer;
    (void)page_table_size;
    if (out != 0) {
        *out = (struct RibonArchDirectHighHandoff){0};
    }
    return RIBON_ARCH_DIRECT_HIGH_UNSUPPORTED;
}

_Noreturn void ribon_arch_enter_kernel(
    uint64_t entry,
    uint64_t handoff,
    uint64_t entry_flags,
    uint64_t bootstrap0) {
    (void)entry;
    (void)handoff;
    (void)entry_flags;
    (void)bootstrap0;
    for (;;) {
    }
}

/** @brief RISC-V instruction view를 `fence.i` 경계로 동기화한다. */
static int riscv64_cache_sync(uint64_t address, uint64_t size) {
    (void)address;
    if (size == 0u) {
        return RIBON_ARCH_OPERATION_BAD_ARGUMENT;
    }
#if defined(__riscv)
    __asm__ __volatile__("fence.i" ::: "memory");
#endif
    return RIBON_ARCH_OPERATION_OK;
}

/** @brief RISC-V hart를 terminal wait loop로 전환한다. */
static _Noreturn void riscv64_halt(void) {
    for (;;) {
#if defined(__riscv)
        __asm__ __volatile__("wfi");
#endif
    }
}

static const struct RibonArchOps riscv64_ops = {
    .size = sizeof(riscv64_ops),
    .abi_version = RIBON_ARCH_OPS_ABI_VERSION,
    .capabilities =
        RIBON_ARCH_CAP_VALIDATE_PAYLOAD |
        RIBON_ARCH_CAP_CACHE_SYNC |
        RIBON_ARCH_CAP_HALT,
    .descriptor = &riscv64_arch,
    .validate_payload = ribon_arch_validate_loaded_payload,
    .cache_sync = riscv64_cache_sync,
    .normalize_privilege = 0,
    .direct_high_page_table_pages = 0,
    .prepare_direct_high_entry = 0,
    .enter_kernel = 0,
    .halt = riscv64_halt,
    .reset = 0,
};

/** @brief 선택된 RISC-V 64 architecture operation table을 반환한다. */
const struct RibonArchOps *ribon_arch_selected_ops(void) {
    return &riscv64_ops;
}

/** @brief Selected RISC-V 64 architecture plugin descriptor다. */
const struct RibonPluginDescriptor ribon_arch_plugin_descriptor = {
    .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_arch_plugin_descriptor),
    .abi_major = RIBON_PLUGIN_ABI_MAJOR,
    .abi_minor = RIBON_PLUGIN_ABI_MINOR,
    .kind = RIBON_PLUGIN_KIND_ARCHITECTURE,
    .phase = RIBON_PLUGIN_PHASE_FOUNDATION,
    .id = "arch.riscv64",
    .provides = RIBON_CAP_ARCHITECTURE,
    .architecture_mask = RIBON_ARCH_MASK_RISCV64,
    .environment_mask = RIBON_ENV_MASK_ALL,
    .mode_mask = RIBON_MODE_MASK_ALL,
    .arena_budget = 4096u,
    .input_budget = 4096u,
    .output_budget = 4096u,
    .deadline_ms = 30000u,
    .operations = &riscv64_ops,
    .operations_size = sizeof(riscv64_ops),
    .operations_abi = RIBON_ARCH_OPS_ABI_VERSION,
    .validate_operations = ribon_arch_plugin_operations_are_valid,
};
