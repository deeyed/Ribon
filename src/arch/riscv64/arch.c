#include <Ribon/arch.h>

static const struct RibonArchDescriptor riscv64_arch = {
    .canonical_name = "riscv64",
    .family_name = "riscv",
    .uefi_binding_dir = "RiscV64",
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
    .firmware_mask = RIBON_ARCH_FIRMWARE_HOST_TEST |
                     RIBON_ARCH_FIRMWARE_UEFI,
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

RIBON_NORETURN void ribon_arch_enter_kernel(
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
static RIBON_NORETURN void riscv64_halt(void) {
    for (;;) {
#if defined(__riscv)
        __asm__ __volatile__("wfi");
#endif
    }
}

static const struct RibonArchOps riscv64_ops = {
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
