#include <Ribon/arch/ops.h>
#include <Ribon/arch/entry.h>
#include <Ribon/boot/image.h>
#include <Ribon/plugin/descriptor.h>

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

/** @brief Architecture descriptor에서 image format별 machine 값을 반환한다. */
static uint16_t expected_machine(
    const struct RibonArchDescriptor *arch,
    enum RibonExecutableFormat format) {
    if (format == RIBON_EXECUTABLE_FORMAT_ELF64) {
        return arch->elf_machine;
    }
    if (format == RIBON_EXECUTABLE_FORMAT_PE_COFF) {
        return arch->pe_coff_machine;
    }
    if (format == RIBON_EXECUTABLE_FORMAT_LINUX_AARCH64 &&
        arch->id == RIBON_ARCHITECTURE_AARCH64) {
        return arch->elf_machine;
    }
    if (format == RIBON_EXECUTABLE_FORMAT_LINUX_RISCV64 &&
        arch->id == RIBON_ARCHITECTURE_RISCV64) {
        return arch->elf_machine;
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

/** @brief Architecture ID를 product compatibility bit로 변환한다. */
uint32_t ribon_architecture_mask(enum RibonArchitectureId architecture) {
    switch (architecture) {
    case RIBON_ARCHITECTURE_X86_64:
        return RIBON_ARCH_MASK_X86_64;
    case RIBON_ARCHITECTURE_AARCH64:
        return RIBON_ARCH_MASK_AARCH64;
    case RIBON_ARCHITECTURE_RISCV64:
        return RIBON_ARCH_MASK_RISCV64;
    default:
        return 0u;
    }
}

/** @brief Architecture operation table ABI와 필수 callback을 검사한다. */
int ribon_arch_ops_are_valid(const struct RibonArchOps *ops) {
    const uint64_t required =
        RIBON_ARCH_CAP_VALIDATE_DIRECT_LOAD |
        RIBON_ARCH_CAP_HALT;

    if (ops == 0 ||
        ops->size != sizeof(*ops) ||
        ops->abi_version != RIBON_ARCH_OPS_ABI_VERSION ||
        (ops->capabilities & ~RIBON_ARCH_CAP_ALL) != 0u ||
        (ops->capabilities & required) != required ||
        ops->descriptor == 0 ||
        ops->descriptor->size != sizeof(*ops->descriptor) ||
        ops->descriptor->abi_version != RIBON_ARCH_OPS_ABI_VERSION ||
        ops->descriptor->canonical_name == 0 ||
        ops->validate_direct_load == 0 ||
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
        (ops->prepare_entry != 0 && ops->transfer_prepared != 0)) {
        return 0;
    }
    if (((ops->capabilities & RIBON_ARCH_CAP_RESET) != 0u) !=
        (ops->reset != 0)) {
        return 0;
    }
    if (((ops->capabilities & RIBON_ARCH_CAP_MONOTONIC_COUNTER) != 0u) !=
        (ops->monotonic_counter != 0)) {
        return 0;
    }
    return 1;
}

/** @brief Architecture plugin descriptor와 operation table을 함께 검사한다. */
int ribon_arch_plugin_operations_are_valid(
    const struct RibonPluginDescriptor *descriptor) {
    const struct RibonArchOps *ops;
    if (descriptor == 0 ||
        descriptor->kind != RIBON_PLUGIN_KIND_ARCHITECTURE ||
        descriptor->operations_size != sizeof(struct RibonArchOps) ||
        descriptor->operations_abi != RIBON_ARCH_OPS_ABI_VERSION ||
        descriptor->provides != RIBON_CAP_ARCHITECTURE) {
        return 0;
    }
    ops = (const struct RibonArchOps *)descriptor->operations;
    return ribon_arch_ops_are_valid(ops) &&
           descriptor->architecture_mask ==
               ribon_architecture_mask(ops->descriptor->id);
}

/** @brief Loaded payload의 공통 machine/canonical address 계약을 검사한다. */
int ribon_arch_validate_direct_load(
    const struct RibonArchDescriptor *arch,
    const struct RibonValidatedImage *image,
    struct RibonDirectLoadPlan *payload) {
    uint16_t machine;
    int entry_covered = 0;

    if (arch == 0 || image == 0 || image->size != sizeof(*image) ||
        image->abi_version != RIBON_VALIDATED_IMAGE_ABI_VERSION ||
        image->format == RIBON_EXECUTABLE_FORMAT_UNKNOWN || image->machine == 0u ||
        image->reserved != 0u || image->image_size == 0u ||
        image->execution_support == 0u ||
        (image->execution_support & ~RIBON_IMAGE_EXECUTION_ALL) != 0u || payload == 0 ||
        payload->size != sizeof(*payload) ||
        payload->abi_version != RIBON_DIRECT_LOAD_PLAN_ABI_VERSION ||
        arch->canonical_name == 0 ||
        arch->word_bits != 64u ||
        arch->page_size == 0u ||
        (image->execution_support & RIBON_IMAGE_EXECUTION_DIRECT_ENTRY) == 0u ||
        (image->format != RIBON_EXECUTABLE_FORMAT_ELF64 &&
         image->format != RIBON_EXECUTABLE_FORMAT_PE_COFF &&
         image->format != RIBON_EXECUTABLE_FORMAT_LINUX_AARCH64 &&
         image->format != RIBON_EXECUTABLE_FORMAT_LINUX_RISCV64) ||
        payload->segments == 0 ||
        payload->segment_count == 0u ||
        payload->segment_count > payload->segment_capacity) {
        return RIBON_ARCH_OPERATION_BAD_ARGUMENT;
    }
    machine = expected_machine(arch, image->format);
    if (machine == 0u || image->machine != machine ||
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
        if ((segment->virtual_address &
             (1ull << (arch->virtual_address_bits - 1u))) != 0u) {
            payload->load_plan_flags |=
                RIBON_LOAD_PLAN_HAS_HIGHER_HALF |
                RIBON_LOAD_PLAN_DIRECT_HIGH_ENTRY_CANDIDATE;
            if (payload->high_entry_virtual_address == 0u ||
                segment->virtual_address < payload->high_entry_virtual_address) {
                payload->high_entry_virtual_address = segment->virtual_address;
                payload->high_entry_load_address = segment->load_address;
            }
        }
    }
    return entry_covered ?
        RIBON_ARCH_OPERATION_OK :
        RIBON_ARCH_OPERATION_INVALID_PAYLOAD;
}

/** @brief Register ABI가 selected architecture와 일치하는지 검사한다. */
static int entry_abi_matches_architecture(
    enum RibonArchitectureId architecture,
    enum RibonRegisterAbi abi) {
    switch (architecture) {
    case RIBON_ARCHITECTURE_X86_64:
        return abi == RIBON_REGISTER_ABI_X86_64_RDI_RSI_RDX_RCX;
    case RIBON_ARCHITECTURE_AARCH64:
        return abi == RIBON_REGISTER_ABI_AARCH64_X0_X1_X2_X3;
    case RIBON_ARCHITECTURE_RISCV64:
        return abi == RIBON_REGISTER_ABI_RISCV64_A0_A1_A2_A3;
    default:
        return 0;
    }
}

/** @brief Entry translation requirement가 architecture의 normal bridge와 맞는지 검사한다. */
static int entry_translation_matches_architecture(
    enum RibonArchitectureId architecture,
    enum RibonEntryTranslationRequirement translation) {
    if (architecture == RIBON_ARCHITECTURE_RISCV64) {
        return translation == RIBON_ENTRY_TRANSLATION_PRESERVE_REACHABLE ||
               translation == RIBON_ENTRY_TRANSLATION_DISABLED;
    }
    if (architecture == RIBON_ARCHITECTURE_X86_64 ||
        architecture == RIBON_ARCHITECTURE_AARCH64) {
        return translation == RIBON_ENTRY_TRANSLATION_PRESERVE_REACHABLE ||
               translation == RIBON_ENTRY_TRANSLATION_DIRECT_HIGH_BRIDGE;
    }
    return 0;
}

/** @brief Protocol invocation을 selected architecture의 prepared entry로 검증한다. */
int ribon_arch_prepare_entry(
    const struct RibonArchDescriptor *arch,
    const struct RibonEntryInvocation *invocation,
    struct RibonPreparedEntry *out) {
    if (arch == 0 || invocation == 0 || out == 0 ||
        invocation->size != sizeof(*invocation) ||
        invocation->abi_version != RIBON_ENTRY_INVOCATION_ABI_VERSION ||
        invocation->entry_address == 0u ||
        invocation->argument_count > RIBON_ENTRY_ARGUMENT_LIMIT ||
        !entry_abi_matches_architecture(arch->id, invocation->register_abi) ||
        invocation->interrupts != RIBON_ENTRY_INTERRUPTS_MASKED ||
        invocation->privilege != RIBON_ENTRY_PRIVILEGE_CURRENT_SUPERVISOR ||
        !entry_translation_matches_architecture(
            arch->id,
            invocation->translation)) {
        if (out != 0) {
            *out = (struct RibonPreparedEntry){0};
        }
        return RIBON_ARCH_OPERATION_BAD_ARGUMENT;
    }
    *out = (struct RibonPreparedEntry){
        .invocation = *invocation,
        .translation_root = 0u,
    };
    return RIBON_ARCH_OPERATION_OK;
}
