#include <Ribon/arch.h>
#include <Ribon/loader.h>

#include <stdint.h>
#include <stdio.h>

#define TEST_PAGE_SIZE 4096ull
#define TEST_ENTRIES 512u
#define TEST_L1_BLOCK 0x40000000ull
#define TEST_L2_BLOCK 0x200000ull
#define TEST_TABLE_BASE 0x100000ull
#define TEST_HIGH_BASE 0xffffffff80000000ull
#define TEST_HIGH_SIZE 0x300000ull
#define TEST_HIGH_LOAD 0x40400000ull
#define TEST_DESC_VALID (1ull << 0)
#define TEST_DESC_TABLE (1ull << 1)
#define TEST_DESC_AF (1ull << 10)
#define TEST_DESC_INNER_SHAREABLE (3ull << 8)
#define TEST_ADDR_MASK 0x0000fffffffff000ull

static uint64_t test_table_desc(uint64_t physical) {
    return (physical & TEST_ADDR_MASK) | TEST_DESC_VALID | TEST_DESC_TABLE;
}

static uint64_t test_block_desc(uint64_t physical) {
    return (physical & TEST_ADDR_MASK) |
           TEST_DESC_INNER_SHAREABLE |
           TEST_DESC_AF |
           TEST_DESC_VALID;
}

static uint64_t test_page_desc(uint64_t physical) {
    return test_block_desc(physical) | TEST_DESC_TABLE;
}

static int expect_u64(const char *name, uint64_t actual, uint64_t expected) {
    if (actual != expected) {
        printf("%s: expected 0x%016llx got 0x%016llx\n",
               name,
               (unsigned long long)expected,
               (unsigned long long)actual);
        return 1;
    }
    return 0;
}

static struct RibonLoadedPayload make_direct_payload(struct RibonLoadSegment *segments) {
    segments[0] = (struct RibonLoadSegment){
        .file_size = TEST_HIGH_SIZE,
        .memory_size = TEST_HIGH_SIZE,
        .virtual_address = TEST_HIGH_BASE,
        .linked_physical_address = TEST_HIGH_LOAD,
        .load_address = TEST_HIGH_LOAD,
        .runtime_address = TEST_HIGH_LOAD,
        .alignment = TEST_PAGE_SIZE,
        .flags = RIBON_LOAD_SEGMENT_READ | RIBON_LOAD_SEGMENT_EXECUTE,
    };
    return (struct RibonLoadedPayload){
        .format = RIBON_EXECUTABLE_FORMAT_ELF64,
        .machine = 183,
        .segment_count = 1,
        .load_plan_flags =
            RIBON_LOAD_PLAN_HAS_HIGHER_HALF |
            RIBON_LOAD_PLAN_DIRECT_HIGH_ENTRY_CANDIDATE |
            RIBON_LOAD_PLAN_SEGMENTS_PLACED |
            RIBON_LOAD_PLAN_RUNTIME_ENTRY_VALID,
        .entry_point = TEST_HIGH_BASE,
        .entry_load_address = TEST_HIGH_LOAD,
        .runtime_entry_address = TEST_HIGH_LOAD,
        .load_base = TEST_HIGH_LOAD,
        .load_end = TEST_HIGH_LOAD + TEST_HIGH_SIZE,
        .runtime_load_base = TEST_HIGH_LOAD,
        .runtime_load_end = TEST_HIGH_LOAD + TEST_HIGH_SIZE,
        .memory_size = TEST_HIGH_SIZE,
        .linked_virtual_base = TEST_HIGH_BASE,
        .linked_virtual_end = TEST_HIGH_BASE + TEST_HIGH_SIZE,
        .linked_physical_base = TEST_HIGH_LOAD,
        .linked_physical_end = TEST_HIGH_LOAD + TEST_HIGH_SIZE,
        .high_entry_virtual_address = TEST_HIGH_BASE,
        .high_entry_load_address = TEST_HIGH_LOAD,
        .segments = segments,
        .segment_capacity = 1,
    };
}

static int test_prepare_direct_high_tables(void) {
    struct RibonLoadSegment segments[1];
    struct RibonLoadedPayload payload = make_direct_payload(segments);
    struct RibonArchDirectHighHandoff handoff;
    uint64_t tables[13u * TEST_ENTRIES];
    uint64_t *identity_l0 = tables;
    uint64_t *identity_l1 = tables + TEST_ENTRIES;
    uint64_t *kernel_l0 = tables + TEST_ENTRIES * 2u;
    uint64_t *kernel_l1 = tables + TEST_ENTRIES * 3u;
    uint64_t *kernel_l2 = tables + TEST_ENTRIES * 4u;
    uint64_t *kernel_l3 = tables + TEST_ENTRIES * 5u;
    const uint32_t l0_index = (uint32_t)((TEST_HIGH_BASE >> 39u) & 0x1ffu);
    const uint32_t l1_index = (uint32_t)((TEST_HIGH_BASE >> 30u) & 0x1ffu);
    const uint32_t l2_index = (uint32_t)((TEST_HIGH_BASE >> 21u) & 0x1ffu);
    const uint32_t l3_index = (uint32_t)((TEST_HIGH_BASE >> 12u) & 0x1ffu);
    int failures = 0;

    failures += expect_u64(
        "direct high table pages",
        ribon_arch_direct_high_page_table_pages(&payload),
        13);
    if (ribon_arch_prepare_direct_high_entry(
            &payload,
            TEST_TABLE_BASE,
            tables,
            sizeof(tables),
            &handoff) != RIBON_ARCH_DIRECT_HIGH_OK) {
        printf("AArch64 direct high prepare failed\n");
        return failures + 1;
    }

    failures += expect_u64("handoff entry", handoff.entry, TEST_HIGH_BASE);
    failures += expect_u64(
        "handoff flags",
        handoff.entry_flags,
        RIBON_KERNEL_ENTRY_FLAG_RPH1 |
            RIBON_KERNEL_ENTRY_FLAG_ENTERED_HIGH |
            RIBON_KERNEL_ENTRY_FLAG_DIRECT_HIGH);
    failures += expect_u64("handoff ttbr0", handoff.bootstrap0, TEST_TABLE_BASE);
    failures += expect_u64("handoff high load", handoff.high_load_start, TEST_HIGH_LOAD);
    failures += expect_u64("identity l0", identity_l0[0], test_table_desc(TEST_TABLE_BASE + TEST_PAGE_SIZE));
    failures += expect_u64("identity l1 0", identity_l1[0], test_block_desc(0));
    failures += expect_u64("identity l1 1", identity_l1[1], test_block_desc(TEST_L1_BLOCK));
    failures += expect_u64(
        "kernel l0",
        kernel_l0[l0_index],
        test_table_desc(TEST_TABLE_BASE + TEST_PAGE_SIZE * 3u));
    failures += expect_u64(
        "kernel l1",
        kernel_l1[l1_index],
        test_table_desc(TEST_TABLE_BASE + TEST_PAGE_SIZE * 4u));
    failures += expect_u64(
        "kernel l2 first",
        kernel_l2[l2_index],
        test_table_desc(TEST_TABLE_BASE + TEST_PAGE_SIZE * 5u));
    failures += expect_u64(
        "kernel l2 second",
        kernel_l2[l2_index + 1u],
        test_table_desc(TEST_TABLE_BASE + TEST_PAGE_SIZE * 6u));
    failures += expect_u64(
        "kernel l3 first",
        kernel_l3[l3_index],
        test_page_desc(TEST_HIGH_LOAD));
    failures += expect_u64(
        "kernel l3 second table first",
        kernel_l3[TEST_ENTRIES],
        test_page_desc(TEST_HIGH_LOAD + TEST_L2_BLOCK));

    return failures;
}

static int test_rejects_missing_direct_candidate(void) {
    struct RibonLoadSegment segments[1];
    struct RibonLoadedPayload payload = make_direct_payload(segments);
    struct RibonArchDirectHighHandoff handoff;
    uint64_t tables[13u * TEST_ENTRIES];
    payload.load_plan_flags &= ~RIBON_LOAD_PLAN_DIRECT_HIGH_ENTRY_CANDIDATE;

    if (ribon_arch_direct_high_page_table_pages(&payload) != 0u) {
        printf("missing direct candidate still reported table pages\n");
        return 1;
    }
    if (ribon_arch_prepare_direct_high_entry(
            &payload,
            TEST_TABLE_BASE,
            tables,
            sizeof(tables),
            &handoff) != RIBON_ARCH_DIRECT_HIGH_BAD_LAYOUT) {
        printf("missing direct candidate was not rejected\n");
        return 1;
    }
    return 0;
}

static int test_rejects_table_misalignment(void) {
    struct RibonLoadSegment segments[1];
    struct RibonLoadedPayload payload = make_direct_payload(segments);
    struct RibonArchDirectHighHandoff handoff;
    uint64_t tables[13u * TEST_ENTRIES];

    if (ribon_arch_prepare_direct_high_entry(
            &payload,
            TEST_TABLE_BASE + 8u,
            tables,
            sizeof(tables),
            &handoff) != RIBON_ARCH_DIRECT_HIGH_BAD_LAYOUT) {
        printf("misaligned table base was not rejected\n");
        return 1;
    }
    return 0;
}

static int test_rejects_l3_capacity_overflow(void) {
    struct RibonLoadSegment segments[1];
    struct RibonLoadedPayload payload = make_direct_payload(segments);
    struct RibonArchDirectHighHandoff handoff;
    uint64_t tables[13u * TEST_ENTRIES];
    payload.linked_virtual_end = TEST_HIGH_BASE + TEST_L2_BLOCK * 9u;

    if (ribon_arch_prepare_direct_high_entry(
            &payload,
            TEST_TABLE_BASE,
            tables,
            sizeof(tables),
            &handoff) != RIBON_ARCH_DIRECT_HIGH_OUT_OF_CAPACITY) {
        printf("L3 capacity overflow was not rejected\n");
        return 1;
    }
    return 0;
}

int main(void) {
    int failures = 0;
    failures += test_prepare_direct_high_tables();
    failures += test_rejects_missing_direct_candidate();
    failures += test_rejects_table_misalignment();
    failures += test_rejects_l3_capacity_overflow();
    if (failures != 0) {
        return 1;
    }
    printf("RIBON-AARCH64-DIRECT-HIGH-TEST-OK\n");
    return 0;
}
