#include <Ribon/arch/ops.h>
#include <Ribon/boot/image.h>
#include <Ribon/plugin/descriptor.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ELF_TEST_BUFFER_SIZE 12288u
#define ELF_TEST_PHDR_OFFSET 64u
#define ELF_TEST_PHDR_SIZE 56u
#define ELF_TEST_MACHINE_X86_64 62u
#define ELF_TEST_MACHINE_AARCH64 183u
#define ELF_TEST_HIGH_BASE 0xffffffff80000000ull

struct TestElfSegment {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
};

static const struct RibonArchDescriptor kX86_64Arch = {
    .size = sizeof(kX86_64Arch),
    .abi_version = RIBON_ARCH_OPS_ABI_VERSION,
    .id = RIBON_ARCHITECTURE_X86_64,
    .canonical_name = "x86_64",
    .family_name = "x86",
    .word_bits = 64,
    .physical_address_bits = 52,
    .virtual_address_bits = 48,
    .elf_machine = ELF_TEST_MACHINE_X86_64,
    .pe_coff_machine = 0x8664u,
    .page_size = 4096,
    .large_page_size = 2097152,
    .kernel_alignment = 2097152,
    .handoff_alignment = 4096,
    .boot_module_alignment = 4096,
    .endian = RIBON_ARCH_ENDIAN_LITTLE,
    .tier = RIBON_ARCH_TIER_PRIMARY,
};

static const struct RibonArchDescriptor kAArch64Arch = {
    .size = sizeof(kAArch64Arch),
    .abi_version = RIBON_ARCH_OPS_ABI_VERSION,
    .id = RIBON_ARCHITECTURE_AARCH64,
    .canonical_name = "aarch64",
    .family_name = "aarch",
    .word_bits = 64,
    .physical_address_bits = 48,
    .virtual_address_bits = 48,
    .elf_machine = ELF_TEST_MACHINE_AARCH64,
    .pe_coff_machine = 0xaa64u,
    .page_size = 4096,
    .large_page_size = 2097152,
    .kernel_alignment = 2097152,
    .handoff_alignment = 4096,
    .boot_module_alignment = 4096,
    .endian = RIBON_ARCH_ENDIAN_LITTLE,
    .tier = RIBON_ARCH_TIER_PRIMARY,
};

static void write_le16(unsigned char *data, uint64_t offset, uint16_t value) {
    data[offset] = (unsigned char)(value & 0xffu);
    data[offset + 1u] = (unsigned char)((value >> 8) & 0xffu);
}

static void write_le32(unsigned char *data, uint64_t offset, uint32_t value) {
    data[offset] = (unsigned char)(value & 0xffu);
    data[offset + 1u] = (unsigned char)((value >> 8) & 0xffu);
    data[offset + 2u] = (unsigned char)((value >> 16) & 0xffu);
    data[offset + 3u] = (unsigned char)((value >> 24) & 0xffu);
}

static void write_le64(unsigned char *data, uint64_t offset, uint64_t value) {
    for (uint32_t index = 0; index < 8u; ++index) {
        data[offset + index] = (unsigned char)((value >> (index * 8u)) & 0xffu);
    }
}

static uint64_t max_u64(uint64_t lhs, uint64_t rhs) {
    return lhs > rhs ? lhs : rhs;
}

static uint64_t build_elf64(
    unsigned char *image,
    uint64_t capacity,
    uint16_t machine,
    uint64_t entry,
    const struct TestElfSegment *segments,
    uint16_t segment_count) {
    uint64_t image_size = ELF_TEST_PHDR_OFFSET + ((uint64_t)segment_count * ELF_TEST_PHDR_SIZE);
    memset(image, 0, (size_t)capacity);
    image[0] = 0x7f;
    image[1] = 'E';
    image[2] = 'L';
    image[3] = 'F';
    image[4] = 2;
    image[5] = 1;
    image[6] = 1;

    write_le16(image, 16, 2);
    write_le16(image, 18, machine);
    write_le32(image, 20, 1);
    write_le64(image, 24, entry);
    write_le64(image, 32, ELF_TEST_PHDR_OFFSET);
    write_le64(image, 40, 0);
    write_le32(image, 48, 0);
    write_le16(image, 52, 64);
    write_le16(image, 54, ELF_TEST_PHDR_SIZE);
    write_le16(image, 56, segment_count);
    write_le16(image, 58, 0);
    write_le16(image, 60, 0);
    write_le16(image, 62, 0);

    for (uint16_t index = 0; index < segment_count; ++index) {
        const struct TestElfSegment *segment = &segments[index];
        const uint64_t phdr = ELF_TEST_PHDR_OFFSET + ((uint64_t)index * ELF_TEST_PHDR_SIZE);
        write_le32(image, phdr + 0u, segment->type);
        write_le32(image, phdr + 4u, segment->flags);
        write_le64(image, phdr + 8u, segment->offset);
        write_le64(image, phdr + 16u, segment->vaddr);
        write_le64(image, phdr + 24u, segment->paddr);
        write_le64(image, phdr + 32u, segment->filesz);
        write_le64(image, phdr + 40u, segment->memsz);
        write_le64(image, phdr + 48u, segment->align);
        image_size = max_u64(image_size, segment->offset + segment->filesz);
    }
    return image_size;
}

static struct RibonValidatedImage analyzed_image;

static int analyze_image(
    const unsigned char *image,
    uint64_t image_size,
    struct RibonDirectLoadPlan *out,
    struct RibonLoadSegment *segments,
    uint32_t segment_capacity) {
    const struct RibonPayloadImage payload = {
        .data = image,
        .size = image_size,
        .source_name = "test.elf",
    };
    memset(out, 0, sizeof(*out));
    memset(segments, 0, sizeof(segments[0]) * segment_capacity);
    out->segments = segments;
    out->segment_capacity = segment_capacity;
    const struct RibonImageFormatOps *ops =
        (const struct RibonImageFormatOps *)
            ribon_elf64_image_plugin_descriptor.operations;
    analyzed_image = (struct RibonValidatedImage){0};
    return ops->analyze(&payload, &analyzed_image, out);
}

static int expect_status(const char *name, int actual, int expected) {
    if (actual != expected) {
        (void)fprintf(stderr, "%s: expected status %d got %d\n", name, expected, actual);
        return 1;
    }
    return 0;
}

static int expect_u64(const char *name, uint64_t actual, uint64_t expected) {
    if (actual != expected) {
        (void)fprintf(stderr, "%s: expected 0x%016llx got 0x%016llx\n",
                      name,
                      (unsigned long long)expected,
                      (unsigned long long)actual);
        return 1;
    }
    return 0;
}

static int expect_u32(const char *name, uint32_t actual, uint32_t expected) {
    if (actual != expected) {
        (void)fprintf(stderr, "%s: expected 0x%08x got 0x%08x\n", name, expected, actual);
        return 1;
    }
    return 0;
}

static int test_higher_half_load_plan(void) {
    unsigned char image[ELF_TEST_BUFFER_SIZE];
    struct RibonLoadSegment segments[4];
    struct RibonDirectLoadPlan payload;
    const struct TestElfSegment fixture[] = {
        {
            .type = 1,
            .flags = 5,
            .offset = 0,
            .vaddr = ELF_TEST_HIGH_BASE,
            .paddr = 0x200000,
            .filesz = 0x90,
            .memsz = 0x1000,
            .align = 0x200000,
        },
    };
    const uint64_t image_size =
        build_elf64(image, sizeof(image), ELF_TEST_MACHINE_X86_64, ELF_TEST_HIGH_BASE + 0x78, fixture, 1);
    int status = analyze_image(image, image_size, &payload, segments, 4);
    if (status == RIBON_LOADER_STATUS_OK &&
        ribon_arch_validate_direct_load(&kX86_64Arch, &analyzed_image, &payload) !=
            RIBON_ARCH_OPERATION_OK) {
        status = RIBON_LOADER_STATUS_UNSUPPORTED;
    }
    const uint32_t expected_flags =
        RIBON_LOAD_PLAN_ENTRY_LOAD_VALID |
        RIBON_LOAD_PLAN_RUNTIME_ENTRY_VALID |
        RIBON_LOAD_PLAN_USES_PADDR |
        RIBON_LOAD_PLAN_HAS_HIGHER_HALF |
        RIBON_LOAD_PLAN_DIRECT_HIGH_ENTRY_CANDIDATE |
        RIBON_LOAD_PLAN_HAS_LINKED_PHYSICAL_RANGE;
    int failures = expect_status("higher-half status", status, RIBON_LOADER_STATUS_OK);
    failures += expect_u32("higher-half segment count", payload.segment_count, 1);
    failures += expect_u32("higher-half flags", payload.load_plan_flags, expected_flags);
    failures += expect_u64("higher-half entry", payload.entry_point, ELF_TEST_HIGH_BASE + 0x78);
    failures += expect_u64("higher-half entry load", payload.entry_load_address, 0x200078);
    failures += expect_u64("higher-half runtime entry", payload.runtime_entry_address, 0x200078);
    failures += expect_u64("higher-half load base", payload.load_base, 0x200000);
    failures += expect_u64("higher-half load end", payload.load_end, 0x201000);
    failures += expect_u64("higher-half runtime load base", payload.runtime_load_base, 0x200000);
    failures += expect_u64("higher-half runtime load end", payload.runtime_load_end, 0x201000);
    failures += expect_u64("higher-half linked vaddr base", payload.linked_virtual_base, ELF_TEST_HIGH_BASE);
    failures += expect_u64("higher-half linked vaddr end", payload.linked_virtual_end, ELF_TEST_HIGH_BASE + 0x1000);
    failures += expect_u64("higher-half linked paddr base", payload.linked_physical_base, 0x200000);
    failures += expect_u64("higher-half linked paddr end", payload.linked_physical_end, 0x201000);
    failures += expect_u64("higher-half direct entry", payload.high_entry_virtual_address, ELF_TEST_HIGH_BASE);
    failures += expect_u64("higher-half direct entry load", payload.high_entry_load_address, 0x200000);
    failures += expect_u64("higher-half segment vaddr", segments[0].virtual_address, ELF_TEST_HIGH_BASE);
    failures += expect_u64("higher-half segment paddr", segments[0].linked_physical_address, 0x200000);
    failures += expect_u64("higher-half segment load", segments[0].load_address, 0x200000);
    failures += expect_u64("higher-half segment runtime", segments[0].runtime_address, 0x200000);
    return failures;
}

static int test_aarch64_machine_success(void) {
    unsigned char image[ELF_TEST_BUFFER_SIZE];
    struct RibonLoadSegment segments[2];
    struct RibonDirectLoadPlan payload;
    const struct TestElfSegment fixture[] = {
        {
            .type = 1,
            .flags = 5,
            .offset = 0,
            .vaddr = 0x400000,
            .paddr = 0x400000,
            .filesz = 0x90,
            .memsz = 0x1000,
            .align = 0x1000,
        },
    };
    const uint64_t image_size =
        build_elf64(image, sizeof(image), ELF_TEST_MACHINE_AARCH64, 0x400078, fixture, 1);
    int status = analyze_image(image, image_size, &payload, segments, 2);
    if (status == RIBON_LOADER_STATUS_OK &&
        ribon_arch_validate_direct_load(&kAArch64Arch, &analyzed_image, &payload) !=
            RIBON_ARCH_OPERATION_OK) {
        status = RIBON_LOADER_STATUS_UNSUPPORTED;
    }
    int failures = expect_status("aarch64 status", status, RIBON_LOADER_STATUS_OK);
    failures += expect_u32("aarch64 machine", analyzed_image.machine, ELF_TEST_MACHINE_AARCH64);
    failures += expect_u64("aarch64 entry load", payload.entry_load_address, 0x400078);
    return failures;
}

static int test_machine_mismatch(void) {
    unsigned char image[ELF_TEST_BUFFER_SIZE];
    struct RibonLoadSegment segments[2];
    struct RibonDirectLoadPlan payload;
    const struct TestElfSegment fixture[] = {
        {
            .type = 1,
            .flags = 5,
            .offset = 0,
            .vaddr = 0x200000,
            .paddr = 0x200000,
            .filesz = 0x90,
            .memsz = 0x1000,
            .align = 0x1000,
        },
    };
    const uint64_t image_size =
        build_elf64(image, sizeof(image), ELF_TEST_MACHINE_AARCH64, 0x200078, fixture, 1);
    const int status = analyze_image(image, image_size, &payload, segments, 2);
    int failures = expect_status("foreign machine parses", status, RIBON_LOADER_STATUS_OK);
    failures += expect_status(
        "machine mismatch validator",
        ribon_arch_validate_direct_load(&kX86_64Arch, &analyzed_image, &payload),
        RIBON_ARCH_OPERATION_INVALID_PAYLOAD);
    return failures;
}

static int test_non_canonical_entry(void) {
    unsigned char image[ELF_TEST_BUFFER_SIZE];
    struct RibonLoadSegment segments[2];
    struct RibonDirectLoadPlan payload;
    const struct TestElfSegment fixture[] = {
        {
            .type = 1,
            .flags = 5,
            .offset = 0,
            .vaddr = 0x0000800000000000ull,
            .paddr = 0x200000,
            .filesz = 0x90,
            .memsz = 0x1000,
            .align = 0x1000,
        },
    };
    const uint64_t image_size =
        build_elf64(image, sizeof(image), ELF_TEST_MACHINE_X86_64, 0x0000800000000078ull, fixture, 1);
    const int status = analyze_image(image, image_size, &payload, segments, 2);
    int failures = expect_status("non-canonical parses", status, RIBON_LOADER_STATUS_OK);
    failures += expect_status(
        "non-canonical validator",
        ribon_arch_validate_direct_load(&kX86_64Arch, &analyzed_image, &payload),
        RIBON_ARCH_OPERATION_INVALID_PAYLOAD);
    return failures;
}

static int test_overlapping_segments(void) {
    unsigned char image[ELF_TEST_BUFFER_SIZE];
    struct RibonLoadSegment segments[4];
    struct RibonDirectLoadPlan payload;
    const struct TestElfSegment fixture[] = {
        {
            .type = 1,
            .flags = 5,
            .offset = 0,
            .vaddr = 0x400000,
            .paddr = 0x400000,
            .filesz = 0x100,
            .memsz = 0x3000,
            .align = 0x1000,
        },
        {
            .type = 1,
            .flags = 4,
            .offset = 0x2000,
            .vaddr = 0x402000,
            .paddr = 0x402000,
            .filesz = 0x80,
            .memsz = 0x1000,
            .align = 0x1000,
        },
    };
    const uint64_t image_size = build_elf64(image, sizeof(image), ELF_TEST_MACHINE_X86_64, 0x400078, fixture, 2);
    const int status = analyze_image(image, image_size, &payload, segments, 4);
    return expect_status("overlapping segments", status, RIBON_LOADER_STATUS_OVERLAPPING_SEGMENTS);
}

static int test_malformed_segment(void) {
    unsigned char image[ELF_TEST_BUFFER_SIZE];
    struct RibonLoadSegment segments[2];
    struct RibonDirectLoadPlan payload;
    const struct TestElfSegment fixture[] = {
        {
            .type = 1,
            .flags = 5,
            .offset = 0,
            .vaddr = 0x200000,
            .paddr = 0x200000,
            .filesz = 0x2000,
            .memsz = 0x1000,
            .align = 0x1000,
        },
    };
    const uint64_t image_size = build_elf64(image, sizeof(image), ELF_TEST_MACHINE_X86_64, 0x200078, fixture, 1);
    const int status = analyze_image(image, image_size, &payload, segments, 2);
    return expect_status("malformed segment", status, RIBON_LOADER_STATUS_BAD_FORMAT);
}

static int test_misaligned_segment(void) {
    unsigned char image[ELF_TEST_BUFFER_SIZE];
    struct RibonLoadSegment segments[2];
    struct RibonDirectLoadPlan payload;
    const struct TestElfSegment fixture[] = {
        {
            .type = 1,
            .flags = 5,
            .offset = 0,
            .vaddr = 0x200008,
            .paddr = 0x200000,
            .filesz = 0x90,
            .memsz = 0x1000,
            .align = 0x1000,
        },
    };
    const uint64_t image_size = build_elf64(image, sizeof(image), ELF_TEST_MACHINE_X86_64, 0x200078, fixture, 1);
    const int status = analyze_image(image, image_size, &payload, segments, 2);
    return expect_status("misaligned segment", status, RIBON_LOADER_STATUS_MISALIGNED);
}

int main(void) {
    int failures = 0;
    failures += test_higher_half_load_plan();
    failures += test_aarch64_machine_success();
    failures += test_machine_mismatch();
    failures += test_non_canonical_entry();
    failures += test_overlapping_segments();
    failures += test_malformed_segment();
    failures += test_misaligned_segment();
    if (failures != 0) {
        (void)fprintf(stderr, "RIBON-LOADER-TEST-FAIL failures=%d\n", failures);
        return 1;
    }
    (void)printf("RIBON-LOADER-TEST-OK\n");
    return 0;
}
