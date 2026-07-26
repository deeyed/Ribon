#include <Ribon/arch.h>
#include <Ribon/core.h>
#include <Ribon/loader.h>
#include <Ribon/platform.h>
#include <Ribon/profile.h>

#include <stdio.h>
#include <string.h>

static unsigned char arena_storage[256u * 1024u + 64u];

static int failures = 0;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures; \
        } \
    } while (0)

static int test_boot_source_read(
    void *context,
    const struct RibonBootSource *source,
    uint64_t offset,
    void *buffer,
    uint64_t size,
    uint64_t deadline_ticks) {
    (void)context;
    (void)source;
    (void)offset;
    (void)deadline_ticks;
    if (buffer == 0 || size == 0u) {
        return RIBON_PLATFORM_STATUS_BAD_ARGUMENT;
    }
    memset(buffer, 0x5a, (size_t)size);
    return RIBON_PLATFORM_STATUS_OK;
}

static int test_timer_now(void *context, uint64_t *ticks_out) {
    (void)context;
    if (ticks_out == 0) {
        return RIBON_PLATFORM_STATUS_BAD_ARGUMENT;
    }
    *ticks_out = 42u;
    return RIBON_PLATFORM_STATUS_OK;
}

static int test_network_fetch(
    void *context,
    const struct RibonBootSource *source,
    uint64_t offset,
    void *buffer,
    uint64_t capacity,
    uint64_t *received_out,
    uint64_t deadline_ticks) {
    (void)context;
    (void)source;
    (void)offset;
    (void)buffer;
    (void)capacity;
    (void)deadline_ticks;
    if (received_out == 0) {
        return RIBON_PLATFORM_STATUS_BAD_ARGUMENT;
    }
    *received_out = 0u;
    return RIBON_PLATFORM_STATUS_OK;
}

static void promote_capability(
    struct RibonPlatformOps *ops,
    uint64_t capability) {
    ops->capabilities.supported |= capability;
    ops->capabilities.unsupported &= ~capability;
    ops->facts.capabilities = ops->capabilities.supported;
}

static void test_arena(void) {
    struct RibonArena arena;
    void *first = 0;
    void *second = 0;
    const uint64_t capacity = sizeof(arena_storage) - 1u;

    ribon_arena_init(&arena, &arena_storage[1], capacity);
    CHECK(ribon_arena_allocate(&arena, 17u, 64u, &first) == RIBON_CORE_STATUS_OK);
    CHECK(first != 0);
    CHECK(((uintptr_t)first & 63u) == 0u);
    CHECK(ribon_arena_allocate(&arena, 31u, 16u, &second) == RIBON_CORE_STATUS_OK);
    CHECK(second != 0);
    CHECK((uintptr_t)second > (uintptr_t)first);
    CHECK(arena.high_watermark == arena.used);
    CHECK(ribon_arena_remaining(&arena) == arena.capacity - arena.used);
    CHECK(ribon_arena_allocate(&arena, 1u, 3u, &second) ==
          RIBON_CORE_STATUS_BAD_ALIGNMENT);
    CHECK(ribon_arena_allocate(&arena, arena.capacity, 1u, &second) ==
          RIBON_CORE_STATUS_OUT_OF_CAPACITY);
}

static void test_platform_and_context(void) {
    struct RibonPlatformOps platform;
    struct RibonArena arena;
    struct RibonCoreContext context;
    struct RibonArchOps arch_without_entry;

    ribon_platform_ops_init_unsupported(
        &platform,
        RIBON_FIRMWARE_HOST,
        "service-boundary-test",
        0);
    CHECK(ribon_platform_ops_are_valid(&platform));
    CHECK(platform.boot_source_read(
              platform.context, 0, 0u, 0, 0u, 0u) ==
          RIBON_PLATFORM_STATUS_UNSUPPORTED);
    promote_capability(&platform, RIBON_PLATFORM_CAP_BOOT_SOURCE_READ);
    CHECK(!ribon_platform_ops_are_valid(&platform));
    ribon_platform_ops_init_unsupported(
        &platform,
        RIBON_FIRMWARE_HOST,
        "service-boundary-test",
        0);

    platform.boot_source_read = test_boot_source_read;
    promote_capability(&platform, RIBON_PLATFORM_CAP_BOOT_SOURCE_READ);
    platform.timer_now = test_timer_now;
    platform.facts.timer_frequency_hz = 1000000u;
    promote_capability(&platform, RIBON_PLATFORM_CAP_MONOTONIC_TIMER);
    CHECK(ribon_platform_ops_are_valid(&platform));
    CHECK(ribon_platform_ops_supports(
        &platform,
        RIBON_PLATFORM_CAP_BOOT_SOURCE_READ |
            RIBON_PLATFORM_CAP_MONOTONIC_TIMER));

    ribon_arena_init(&arena, arena_storage, 256u * 1024u);
    context = (struct RibonCoreContext){
        .mode = ribon_mode_selected(),
        .platform = &platform,
        .arch = ribon_arch_selected_ops(),
        .profile = ribon_profile_parus(),
        .arena = &arena,
    };
    CHECK(ribon_core_context_validate(&context) == RIBON_CORE_STATUS_OK);
    arch_without_entry = *context.arch;
    arch_without_entry.capabilities &= ~RIBON_ARCH_CAP_ENTRY_BRIDGE;
    arch_without_entry.enter_kernel = 0;
    context.arch = &arch_without_entry;
    CHECK(ribon_core_context_validate(&context) ==
          RIBON_CORE_STATUS_MISSING_CAPABILITY);
    context.arch = ribon_arch_selected_ops();
    --arena.capacity;
    CHECK(ribon_core_context_validate(&context) == RIBON_CORE_STATUS_BAD_LIMIT);
    ++arena.capacity;

    platform.network_fetch = test_network_fetch;
    promote_capability(&platform, RIBON_PLATFORM_CAP_NETWORK_TRANSPORT);
    CHECK(ribon_core_context_validate(&context) ==
          RIBON_CORE_STATUS_FORBIDDEN_CAPABILITY);

    ribon_platform_ops_init_unsupported(
        &platform,
        RIBON_FIRMWARE_HOST,
        "service-boundary-test",
        0);
    platform.boot_source_read = test_boot_source_read;
    promote_capability(&platform, RIBON_PLATFORM_CAP_BOOT_SOURCE_READ);
    CHECK(ribon_core_context_validate(&context) ==
          RIBON_CORE_STATUS_MISSING_CAPABILITY);
}

static void test_arch_ops(void) {
    struct RibonLoadSegment segment = {
        .file_size = 0x1000u,
        .memory_size = 0x1000u,
        .virtual_address = 0x200000u,
        .flags = RIBON_LOAD_SEGMENT_EXECUTE,
    };
    struct RibonLoadedPayload payload = {
        .format = RIBON_EXECUTABLE_FORMAT_ELF64,
        .machine = 62u,
        .segment_count = 1u,
        .entry_point = 0x200078u,
        .segments = &segment,
        .segment_capacity = 1u,
    };
    const struct RibonArchOps *ops = ribon_arch_selected_ops();
    struct RibonArchOps invalid_ops;

    CHECK(ribon_arch_ops_are_valid(ops));
    invalid_ops = *ops;
    invalid_ops.capabilities &= ~RIBON_ARCH_CAP_CACHE_SYNC;
    CHECK(!ribon_arch_ops_are_valid(&invalid_ops));
    CHECK(strcmp(ops->descriptor->canonical_name, "x86_64") == 0);
    CHECK(ops->validate_payload(ops->descriptor, &payload) ==
          RIBON_ARCH_OPERATION_OK);
    payload.entry_point = 0x0000800000000000ull;
    CHECK(ops->validate_payload(ops->descriptor, &payload) ==
          RIBON_ARCH_OPERATION_INVALID_PAYLOAD);
}

static void fill_nonce(uint8_t nonce[RIBON_BOOT_CONFIRMATION_NONCE_SIZE]) {
    for (uint32_t index = 0; index < RIBON_BOOT_CONFIRMATION_NONCE_SIZE; ++index) {
        nonce[index] = (uint8_t)(index * 7u + 3u);
    }
}

static void test_profile_ops(void) {
    const struct RibonProfile *profile = ribon_profile_parus();
    struct RibonComponentDescriptor components[] = {
        {
            .role = RIBON_COMPONENT_ROLE_KERNEL,
            .name = "kernel",
            .size = 4096u,
        },
        {
            .role = RIBON_COMPONENT_ROLE_BOOT_MODULE,
            .name = "init",
            .size = 1024u,
        },
    };
    struct RibonManifestView manifest = {
        .profile_id = "parus",
        .profile_abi_min = 1u,
        .profile_abi_max = 1u,
        .components = components,
        .component_count = 2u,
    };
    struct RibonEntryContract entry_contract;
    struct RibonBootConfirmation confirmation = {
        .profile_id = "parus",
        .generation = 9u,
        .nonce_size = RIBON_BOOT_CONFIRMATION_NONCE_SIZE,
        .result = RIBON_BOOT_CONFIRMATION_HEALTHY,
    };
    struct RibonBootConfirmationExpectation expected = {
        .generation = 9u,
        .nonce_size = RIBON_BOOT_CONFIRMATION_NONCE_SIZE,
    };
    struct RibonProfile invalid_profile;

    CHECK(ribon_profile_is_valid(profile));
    invalid_profile = *profile;
    invalid_profile.capabilities &= ~RIBON_PROFILE_CAP_HANDOFF;
    CHECK(!ribon_profile_is_valid(&invalid_profile));
    CHECK(ribon_profile_has_capability(profile, RIBON_PROFILE_CAP_ALL));
    CHECK(profile->ops->match_manifest(&manifest) == RIBON_PROFILE_STATUS_OK);
    CHECK(profile->ops->validate_components(&manifest) == RIBON_PROFILE_STATUS_OK);
    CHECK(profile->ops->select_entry_contract(
              ribon_arch_selected(), &entry_contract) ==
          RIBON_PROFILE_STATUS_OK);
    CHECK(entry_contract.register_abi == RIBON_REGISTER_ABI_X86_64_RDI_RSI);
    CHECK(entry_contract.required_entry_flags == RIBON_KERNEL_ENTRY_FLAG_RPH1);

    components[1].role = RIBON_COMPONENT_ROLE_KERNEL;
    CHECK(profile->ops->validate_components(&manifest) ==
          RIBON_PROFILE_STATUS_BAD_COMPONENTS);
    components[1].role = RIBON_COMPONENT_ROLE_BOOT_MODULE;
    components[1].flags = 1u;
    CHECK(profile->ops->validate_components(&manifest) ==
          RIBON_PROFILE_STATUS_BAD_COMPONENTS);
    components[1].flags = RIBON_COMPONENT_FLAGS_NONE;

    fill_nonce(confirmation.nonce);
    fill_nonce(expected.nonce);
    CHECK(ribon_profile_validate_confirmation(
              profile, &confirmation, &expected) ==
          RIBON_PROFILE_STATUS_OK);
    confirmation.nonce[RIBON_BOOT_CONFIRMATION_NONCE_SIZE - 1u] ^= 1u;
    CHECK(ribon_profile_validate_confirmation(
              profile, &confirmation, &expected) ==
          RIBON_PROFILE_STATUS_BAD_CONFIRMATION);
}

int main(void) {
    CHECK(ribon_mode_selected()->mode == RIBON_MODE_NORMAL);
    CHECK(ribon_mode_descriptor_is_valid(ribon_mode_selected()));
    CHECK(strcmp(ribon_mode_name(RIBON_MODE_RECOVERY), "recovery") == 0);
    test_arena();
    test_platform_and_context();
    test_arch_ops();
    test_profile_ops();

    if (failures != 0) {
        fprintf(stderr, "service_boundary_tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("RIBON-R2-CORE-SERVICE-BOUNDARY-OK");
    return 0;
}
