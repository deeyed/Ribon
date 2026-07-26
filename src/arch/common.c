#include <Ribon/arch.h>
#include <Ribon/loader.h>

/** @brief 두 C 문자열이 같은지 검사한다. */
static int arch_streq(const char *lhs, const char *rhs) {
    if (lhs == 0 || rhs == 0) {
        return 0;
    }
    while (*lhs != '\0' && *rhs != '\0') {
        if (*lhs != *rhs) {
            return 0;
        }
        ++lhs;
        ++rhs;
    }
    return *lhs == *rhs;
}

/** @brief 주소가 architecture virtual address 폭에 맞는 canonical 값인지 검사한다. */
static int address_is_canonical(uint64_t address, uint32_t virtual_address_bits) {
    uint64_t high_mask;
    uint64_t sign_bit;

    if (virtual_address_bits == 0u || virtual_address_bits > 64u) {
        return 0;
    }
    if (virtual_address_bits == 64u) {
        return 1;
    }
    sign_bit = 1ull << (virtual_address_bits - 1u);
    high_mask = ~((1ull << virtual_address_bits) - 1ull);
    if ((address & sign_bit) == 0u) {
        return (address & high_mask) == 0u;
    }
    return (address & high_mask) == high_mask;
}

/** @brief Architecture 이름에 대응하는 ELF machine 값을 반환한다. */
static uint16_t expected_machine(const struct RibonArchDescriptor *arch) {
    if (arch_streq(arch->canonical_name, "x86_64")) {
        return 62u;
    }
    if (arch_streq(arch->canonical_name, "aarch64")) {
        return 183u;
    }
    if (arch_streq(arch->canonical_name, "riscv64")) {
        return 243u;
    }
    return 0u;
}

/** @brief Architecture tier 열거값의 고정 문자열을 반환한다. */
const char *ribon_arch_tier_name(enum RibonArchTier tier) {
    switch (tier) {
    case RIBON_ARCH_TIER_PRIMARY:
        return "primary";
    case RIBON_ARCH_TIER_FUTURE:
        return "future";
    default:
        return "unknown";
    }
}

/** @brief Architecture endian 열거값의 고정 문자열을 반환한다. */
const char *ribon_arch_endian_name(enum RibonArchEndian endian) {
    switch (endian) {
    case RIBON_ARCH_ENDIAN_LITTLE:
        return "little";
    default:
        return "unknown";
    }
}

/** @brief Architecture가 요청한 firmware mask를 모두 지원하는지 검사한다. */
int ribon_arch_has_firmware_mask(const struct RibonArchDescriptor *arch, uint32_t mask) {
    if (arch == 0) {
        return 0;
    }
    return (arch->firmware_mask & mask) == mask;
}

/** @brief Architecture operation table ABI와 필수 callback을 검사한다. */
int ribon_arch_ops_are_valid(const struct RibonArchOps *ops) {
    const uint64_t required =
        RIBON_ARCH_CAP_VALIDATE_PAYLOAD |
        RIBON_ARCH_CAP_HALT;

    if (ops == 0 ||
        ops->abi_version != RIBON_ARCH_OPS_ABI_VERSION ||
        (ops->capabilities & ~RIBON_ARCH_CAP_ALL) != 0u ||
        (ops->capabilities & required) != required ||
        ops->descriptor == 0 ||
        ops->descriptor->canonical_name == 0 ||
        ops->validate_payload == 0 ||
        ops->halt == 0) {
        return 0;
    }
    if (((ops->capabilities & RIBON_ARCH_CAP_CACHE_SYNC) != 0u) !=
        (ops->cache_sync != 0)) {
        return 0;
    }
    if (((ops->capabilities & RIBON_ARCH_CAP_PRIVILEGE_NORMALIZE) != 0u) !=
        (ops->normalize_privilege != 0)) {
        return 0;
    }
    if (((ops->capabilities & RIBON_ARCH_CAP_DIRECT_HIGH_ENTRY) != 0u) !=
        (ops->direct_high_page_table_pages != 0 &&
         ops->prepare_direct_high_entry != 0)) {
        return 0;
    }
    if (((ops->capabilities & RIBON_ARCH_CAP_ENTRY_BRIDGE) != 0u) !=
        (ops->enter_kernel != 0)) {
        return 0;
    }
    if (((ops->capabilities & RIBON_ARCH_CAP_RESET) != 0u) !=
        (ops->reset != 0)) {
        return 0;
    }
    return 1;
}

/** @brief Loaded payload의 공통 machine/canonical address 계약을 검사한다. */
int ribon_arch_validate_loaded_payload(
    const struct RibonArchDescriptor *arch,
    const struct RibonLoadedPayload *payload) {
    uint16_t machine;
    int entry_covered = 0;

    if (arch == 0 || payload == 0 ||
        arch->canonical_name == 0 ||
        arch->word_bits != 64u ||
        arch->page_size == 0u ||
        payload->format != RIBON_EXECUTABLE_FORMAT_ELF64 ||
        payload->segments == 0 ||
        payload->segment_count == 0u ||
        payload->segment_count > payload->segment_capacity) {
        return RIBON_ARCH_OPERATION_BAD_ARGUMENT;
    }
    machine = expected_machine(arch);
    if (machine == 0u || payload->machine != machine ||
        !address_is_canonical(payload->entry_point, arch->virtual_address_bits)) {
        return RIBON_ARCH_OPERATION_INVALID_PAYLOAD;
    }
    for (uint32_t index = 0; index < payload->segment_count; ++index) {
        const struct RibonLoadSegment *segment = &payload->segments[index];
        uint64_t virtual_end;
        if (segment->memory_size == 0u ||
            segment->file_size > segment->memory_size ||
            !address_is_canonical(segment->virtual_address, arch->virtual_address_bits) ||
            segment->virtual_address > UINT64_MAX - segment->memory_size) {
            return RIBON_ARCH_OPERATION_INVALID_PAYLOAD;
        }
        virtual_end = segment->virtual_address + segment->memory_size;
        if (!address_is_canonical(virtual_end - 1u, arch->virtual_address_bits)) {
            return RIBON_ARCH_OPERATION_INVALID_PAYLOAD;
        }
        if (payload->entry_point >= segment->virtual_address &&
            payload->entry_point < virtual_end &&
            (segment->flags & RIBON_LOAD_SEGMENT_EXECUTE) != 0u) {
            entry_covered = 1;
        }
    }
    return entry_covered ?
        RIBON_ARCH_OPERATION_OK :
        RIBON_ARCH_OPERATION_INVALID_PAYLOAD;
}
