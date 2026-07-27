#include <Ribon/core/context.h>
#include <Ribon/service/directory.h>

#include "../../src/environments/host/host.h"

#include <stdio.h>

static int failures;
static unsigned char arena_storage[256u * 1024u + 64u];

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++failures; \
        } \
    } while (0)

static void test_arena(void) {
    struct RibonArena arena;
    void *first = 0;
    void *second = 0;
    ribon_arena_init(&arena, &arena_storage[1], sizeof(arena_storage) - 1u);
    CHECK(ribon_arena_allocate(&arena, 17u, 64u, &first) ==
          RIBON_CORE_STATUS_OK);
    CHECK(first != 0 && ((uintptr_t)first & 63u) == 0u);
    CHECK(ribon_arena_allocate(&arena, 31u, 16u, &second) ==
          RIBON_CORE_STATUS_OK);
    CHECK(second != 0 && (uintptr_t)second > (uintptr_t)first);
    CHECK(ribon_arena_allocate(&arena, 1u, 3u, &second) ==
          RIBON_CORE_STATUS_BAD_ALIGNMENT);
}

static void test_services(void) {
    const struct RibonServiceDirectory *services =
        ribon_generated_service_directory();
    struct RibonServiceDirectory invalid = *services;
    CHECK(ribon_service_directory_validate(
              services,
              ribon_generated_product_descriptor(),
              RIBON_MODE_NORMAL) == RIBON_CORE_STATUS_OK);
    CHECK(ribon_service_directory_find_exact(
              services,
              RIBON_SERVICE_KIND_BOOT_SOURCE,
              "service.host.boot-source") != 0);
    CHECK(ribon_service_directory_find_exact(
              services,
              RIBON_SERVICE_KIND_MONOTONIC_TIMER,
              "service.host.monotonic-timer") != 0);
    invalid.abi_version += 1u;
    CHECK(ribon_service_directory_validate(
              &invalid,
              ribon_generated_product_descriptor(),
              RIBON_MODE_NORMAL) == RIBON_CORE_STATUS_INVALID_DESCRIPTOR);
}

static void test_context(void) {
    struct RibonArena arena;
    struct RibonCoreContext context;
    struct RibonProductDescriptor invalid_product =
        *ribon_generated_product_descriptor();

    ribon_arena_init(&arena, arena_storage, 256u * 1024u);
    CHECK(ribon_context_initialize(
              &context,
              ribon_generated_product_descriptor(),
              ribon_generated_plugin_registry(),
              ribon_generated_service_directory(),
              ribon_mode_selected(),
              &arena) == RIBON_CORE_STATUS_OK);
    CHECK(context.product == ribon_generated_product_descriptor());
    CHECK(context.registry == ribon_generated_plugin_registry());
    --arena.capacity;
    CHECK(ribon_core_context_validate(&context) == RIBON_CORE_STATUS_BAD_LIMIT);
    ++arena.capacity;

    invalid_product.allowed_capabilities &= ~RIBON_CAP_IMAGE_ELF64;
    CHECK(ribon_context_initialize(
              &context,
              &invalid_product,
              ribon_generated_plugin_registry(),
              ribon_generated_service_directory(),
              ribon_mode_selected(),
              &arena) == RIBON_CORE_STATUS_INVALID_DESCRIPTOR);
}

int main(void) {
    test_arena();
    test_services();
    test_context();
    if (failures != 0) {
        fprintf(stderr, "service_boundary_tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("RIBON-R6-TYPED-SERVICE-DIRECTORY-OK");
    return 0;
}
