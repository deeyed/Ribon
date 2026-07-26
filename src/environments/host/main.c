#include "host.h"

#include <Ribon/arch/ops.h>
#include <Ribon/boot/plan.h>
#include <Ribon/boot/transfer.h>
#include <Ribon/core/context.h>
#include <Ribon/platform/facts.h>
#include <Ribon/plugin/registry.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct HostPayloadBuffer {
    unsigned char *data;
    uint64_t size;
};

/** @brief Host file을 bounded caller-owned payload view로 읽는다. */
static int read_host_payload(const char *path, struct HostPayloadBuffer *out) {
    FILE *file;
    long size;
    size_t read_size;
    unsigned char *data;

    if (path == 0 || out == 0) {
        return -1;
    }
    *out = (struct HostPayloadBuffer){0};
    file = fopen(path, "rb");
    if (file == 0) {
        return -1;
    }
    if (fseek(file, 0, SEEK_END) != 0 ||
        (size = ftell(file)) <= 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }
    data = (unsigned char *)malloc((size_t)size);
    if (data == 0) {
        fclose(file);
        return -1;
    }
    read_size = fread(data, 1u, (size_t)size, file);
    fclose(file);
    if (read_size != (size_t)size) {
        free(data);
        return -1;
    }
    out->data = data;
    out->size = (uint64_t)size;
    return 0;
}

/** @brief Host reference plan의 stable diagnostic fields를 출력한다. */
static void print_plan(
    const struct RibonCoreContext *core,
    const struct RibonBootSession *session,
    const struct RibonBootPlan *plan) {
    printf("ribon=%s\n", ribon_version_string());
    printf("product=%s\n", core->product->id);
    printf("registry-plugins=%u\n", core->registry->plugin_count);
    printf("core-library=libribon-core\n");
    printf("boot-library=libribon-boot\n");
    printf("environment=%s\n", ribon_environment_name(plan->environment));
    printf("platform=%s\n", ribon_platform_selected()->id);
    printf("arch=%s\n", plan->arch->canonical_name);
    printf("protocol=%s\n", plan->protocol_id);
    printf("image-format=%s\n", ribon_executable_format_name(plan->kernel_format));
    printf("kernel=%s\n", plan->kernel_path);
    printf("kernel-source=%s\n", plan->kernel_source_name);
    printf("memory-regions=%u\n", plan->memory_region_count);
    printf("normalized-memory-regions=%u\n", plan->normalized_memory_region_count);
    printf("usable-memory=0x%016llx\n", (unsigned long long)plan->usable_memory_bytes);
    printf("kernel-load-segments=%u\n", plan->kernel_load_segment_count);
    printf("kernel-runtime-entry=0x%016llx\n",
           (unsigned long long)plan->kernel_runtime_entry_address);
    printf("handoff-format=%s\n", plan->handoff_artifact_format);
    printf("handoff-size=%llu\n", (unsigned long long)plan->handoff_artifact_size);
    printf("session-state=%u\n", (unsigned)session->state);
}

/** @brief Generated host product graph로 protocol-neutral boot plan을 만든다. */
int main(int argc, char **argv) {
    const char *kernel_path = 0;
    const struct RibonPluginRegistry *registry =
        ribon_generated_plugin_registry();
    const struct RibonProductDescriptor *product =
        ribon_generated_product_descriptor();
    const struct RibonPluginDescriptor *protocol_plugin;
    const struct RibonPluginDescriptor *image_plugin;
    const struct RibonBootProtocol *protocol;
    const struct RibonImageFormatOps *image_format;
    const struct RibonServiceTable *services = ribon_host_services();
    const struct RibonArchOps *arch = ribon_arch_selected_ops();
    unsigned char arena_storage[256u * 1024u];
    struct RibonArena arena;
    struct RibonCoreContext core;
    struct RibonBootSession session;
    struct RibonBootEnvironment environment;
    struct HostPayloadBuffer kernel_buffer;
    struct RibonLoadSegment kernel_segments[16];
    struct RibonLoadedPayload kernel_layout = {
        .segments = kernel_segments,
        .segment_capacity =
            (uint32_t)(sizeof(kernel_segments) / sizeof(kernel_segments[0])),
    };
    struct RibonMemoryRegion normalized_regions[32];
    struct RibonMutableMemoryMap normalized_map = {
        .regions = normalized_regions,
        .capacity =
            (uint32_t)(sizeof(normalized_regions) / sizeof(normalized_regions[0])),
    };
    unsigned char handoff_buffer[64u * 1024u];
    struct RibonHandoffArtifact handoff = {0};
    struct RibonBootPlan plan;
    int status;

    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--kernel") == 0 && index + 1 < argc) {
            kernel_path = argv[++index];
            continue;
        }
        fprintf(stderr, "usage: %s --kernel path\n", argv[0]);
        return 2;
    }
    if (kernel_path == 0) {
        fputs("kernel path is required\n", stderr);
        return 2;
    }

    protocol_plugin = ribon_plugin_registry_find(
        registry,
        RIBON_PLUGIN_KIND_BOOT_PROTOCOL,
        "protocol.synthetic");
    image_plugin = ribon_plugin_registry_find(
        registry,
        RIBON_PLUGIN_KIND_IMAGE_FORMAT,
        "image.elf64");
    if (protocol_plugin == 0 || image_plugin == 0) {
        fputs("generated registry is incomplete\n", stderr);
        return 1;
    }
    protocol = (const struct RibonBootProtocol *)protocol_plugin->operations;
    image_format = (const struct RibonImageFormatOps *)image_plugin->operations;

    ribon_arena_init(&arena, arena_storage, sizeof(arena_storage));
    status = ribon_context_initialize(
        &core,
        product,
        registry,
        ribon_mode_selected(),
        &arena);
    if (status != RIBON_CORE_STATUS_OK) {
        fprintf(stderr, "context initialization failed: %d\n", status);
        return 1;
    }
    status = ribon_boot_session_initialize(
        &session,
        &core,
        services,
        arch,
        protocol,
        image_format);
    if (status != RIBON_BOOT_STATUS_OK) {
        fprintf(stderr, "session initialization failed: %d\n", status);
        return 1;
    }
    if (ribon_host_environment_collect(arch->descriptor->id, &environment) !=
        RIBON_SERVICE_STATUS_OK) {
        fputs("host environment collection failed\n", stderr);
        return 1;
    }
    if (read_host_payload(kernel_path, &kernel_buffer) != 0) {
        fprintf(stderr, "payload read failed: %s\n", kernel_path);
        return 1;
    }

    status = ribon_boot_prepare(
        &session,
        &(const struct RibonBootRequest){
            .environment = &environment,
            .normalized_memory_map = &normalized_map,
            .kernel_payload =
                &(const struct RibonPayloadImage){
                    .data = kernel_buffer.data,
                    .size = kernel_buffer.size,
                    .source_name = kernel_path,
                },
            .kernel_layout = &kernel_layout,
            .handoff_buffer = handoff_buffer,
            .handoff_buffer_capacity = sizeof(handoff_buffer),
            .handoff_artifact = &handoff,
        },
        &plan);
    if (status == RIBON_BOOT_STATUS_OK) {
        status = ribon_boot_commit(&session);
    }
    if (status == RIBON_BOOT_STATUS_OK) {
        status = ribon_environment_quiesce(&session);
    }
    if (status != RIBON_BOOT_STATUS_OK) {
        fprintf(stderr, "boot plan failed: %d\n", status);
        free(kernel_buffer.data);
        return 1;
    }
    print_plan(&core, &session, &plan);
    free(kernel_buffer.data);
    return 0;
}
