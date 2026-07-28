#include <Ribon/arch/entry.h>
#include <Ribon/arch/ops.h>
#include <Ribon/boot/image.h>

#include <stdint.h>
#include <stdio.h>

#define TEST_PAGE_SIZE 4096ull
#define TEST_ENTRIES 512u
#define TEST_LARGE_2M 0x200000ull
#define TEST_TABLE_BASE 0x100000ull
#define TEST_HIGH_BASE 0xffffffff80000000ull
#define TEST_HIGH_SIZE 0x400000ull
#define TEST_HIGH_LOAD 0x400000ull
#define TEST_PRESENT (1ull << 0)
#define TEST_WRITE (1ull << 1)
#define TEST_LARGE (1ull << 7)
#define TEST_ADDR_MASK 0x000ffffffffffff000ull

static uint64_t test_table_desc(uint64_t physical) {
    return (physical & TEST_ADDR_MASK) | TEST_PRESENT | TEST_WRITE;
}

static uint64_t test_large_desc(uint64_t physical) {
    return test_table_desc(physical) | TEST_LARGE;
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
        .file_size = 0x1000,
        .memory_size = 0x1000,
        .virtual_address = 0x200000,
        .linked_physical_address = 0x200000,
        .load_address = 0x200000,
        .runtime_address = 0x200000,
        .alignment = TEST_LARGE_2M,
        .flags = RIBON_LOAD_SEGMENT_READ | RIBON_LOAD_SEGMENT_EXECUTE,
    };
    segments[1] = (struct RibonLoadSegment){
        .file_size = TEST_HIGH_SIZE,
        .memory_size = TEST_HIGH_SIZE,
        .virtual_address = TEST_HIGH_BASE,
        .linked_physical_address = TEST_HIGH_LOAD,
        .load_address = TEST_HIGH_LOAD,
        .runtime_address = TEST_HIGH_LOAD,
        .alignment = TEST_LARGE_2M,
        .flags = RIBON_LOAD_SEGMENT_READ | RIBON_LOAD_SEGMENT_EXECUTE,
    };
    return (struct RibonLoadedPayload){
        .format = RIBON_EXECUTABLE_FORMAT_ELF64,
        .machine = 62,
        .segment_count = 2,
        .load_plan_flags =
            RIBON_LOAD_PLAN_HAS_HIGHER_HALF |
            RIBON_LOAD_PLAN_DIRECT_HIGH_ENTRY_CANDIDATE |
            RIBON_LOAD_PLAN_SEGMENTS_PLACED |
            RIBON_LOAD_PLAN_RUNTIME_ENTRY_VALID,
        .entry_point = 0x200000,
        .entry_load_address = 0x200000,
        .runtime_entry_address = 0x200000,
        .load_base = 0x200000,
        .load_end = TEST_HIGH_LOAD + TEST_HIGH_SIZE,
        .runtime_load_base = 0x200000,
        .runtime_load_end = TEST_HIGH_LOAD + TEST_HIGH_SIZE,
        .memory_size = TEST_HIGH_SIZE,
        .linked_virtual_base = TEST_HIGH_BASE,
        .linked_virtual_end = TEST_HIGH_BASE + TEST_HIGH_SIZE,
        .linked_physical_base = TEST_HIGH_LOAD,
        .linked_physical_end = TEST_HIGH_LOAD + TEST_HIGH_SIZE,
        .high_entry_virtual_address = TEST_HIGH_BASE,
        .high_entry_load_address = TEST_HIGH_LOAD,
        .segments = segments,
        .segment_capacity = 2,
    };
}

static int test_prepare_direct_high_tables(void) {
    struct RibonLoadSegment segments[2];
    struct RibonLoadedPayload payload = make_direct_payload(segments);
    struct RibonArchDirectHighHandoff handoff;
    uint64_t tables[8u * TEST_ENTRIES];
    uint64_t *pml4 = tables;
    uint64_t *low_pdpt = tables + TEST_ENTRIES;
    uint64_t *low_pd0 = tables + TEST_ENTRIES * 2u;
    uint64_t *high_pdpt = tables + TEST_ENTRIES * 6u;
    uint64_t *high_pd = tables + TEST_ENTRIES * 7u;
    const uint32_t pml4_index = (uint32_t)((TEST_HIGH_BASE >> 39u) & 0x1ffu);
    const uint32_t pdpt_index = (uint32_t)((TEST_HIGH_BASE >> 30u) & 0x1ffu);
    const uint32_t pd_index = (uint32_t)((TEST_HIGH_BASE >> 21u) & 0x1ffu);
    int failures = 0;

    failures += expect_u64(
        "direct high table pages",
        ribon_arch_direct_high_page_table_pages(&payload),
        8);
    if (ribon_arch_prepare_direct_high_entry(
            &payload,
            TEST_TABLE_BASE,
            tables,
            sizeof(tables),
            &handoff) != RIBON_ARCH_DIRECT_HIGH_OK) {
        printf("direct high prepare failed\n");
        return failures + 1;
    }

    failures += expect_u64("handoff entry", handoff.entry, TEST_HIGH_BASE);
    failures += expect_u64(
        "handoff translation root",
        handoff.translation_root,
        TEST_TABLE_BASE);
    failures += expect_u64("handoff high load", handoff.high_load_start, TEST_HIGH_LOAD);
    failures += expect_u64("pml4 low", pml4[0], test_table_desc(TEST_TABLE_BASE + TEST_PAGE_SIZE));
    failures += expect_u64(
        "low pdpt 0",
        low_pdpt[0],
        test_table_desc(TEST_TABLE_BASE + TEST_PAGE_SIZE * 2u));
    failures += expect_u64("low pd0 1", low_pd0[1], test_large_desc(TEST_LARGE_2M));
    failures += expect_u64(
        "pml4 high",
        pml4[pml4_index],
        test_table_desc(TEST_TABLE_BASE + TEST_PAGE_SIZE * 6u));
    failures += expect_u64(
        "high pdpt",
        high_pdpt[pdpt_index],
        test_table_desc(TEST_TABLE_BASE + TEST_PAGE_SIZE * 7u));
    failures += expect_u64("high pd first", high_pd[pd_index], test_large_desc(TEST_HIGH_LOAD));
    failures += expect_u64(
        "high pd second",
        high_pd[pd_index + 1u],
        test_large_desc(TEST_HIGH_LOAD + TEST_LARGE_2M));

    return failures;
}

static int test_rejects_missing_direct_candidate(void) {
    struct RibonLoadSegment segments[2];
    struct RibonLoadedPayload payload = make_direct_payload(segments);
    struct RibonArchDirectHighHandoff handoff;
    uint64_t tables[8u * TEST_ENTRIES];
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

static int test_rejects_pdpt_crossing_range(void) {
    struct RibonLoadSegment segments[2];
    struct RibonLoadedPayload payload = make_direct_payload(segments);
    struct RibonArchDirectHighHandoff handoff;
    uint64_t tables[8u * TEST_ENTRIES];
    payload.linked_virtual_end = TEST_HIGH_BASE + 0x40200000ull;

    if (ribon_arch_prepare_direct_high_entry(
            &payload,
            TEST_TABLE_BASE,
            tables,
            sizeof(tables),
            &handoff) != RIBON_ARCH_DIRECT_HIGH_BAD_LAYOUT) {
        printf("PDPT-crossing direct range was not rejected\n");
        return 1;
    }
    return 0;
}

int main(void) {
    int failures = 0;
    failures += test_prepare_direct_high_tables();
    failures += test_rejects_missing_direct_candidate();
    failures += test_rejects_pdpt_crossing_range();
    if (failures != 0) {
        return 1;
    }
    printf("RIBON-X86_64-DIRECT-HIGH-TEST-OK\n");
    return 0;
}
