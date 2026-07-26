#include <Ribon/ribon.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct HostPayloadBuffer {
    unsigned char *data;
    uint64_t size;
};

static const char *status_word(int enabled) {
    return enabled ? "required" : "optional";
}

static int read_host_payload(const char *path, struct HostPayloadBuffer *out) {
    FILE *file;
    long size;
    size_t read_size;
    unsigned char *data;
    if (path == 0 || out == 0) {
        return -1;
    }
    out->data = 0;
    out->size = 0;
    file = fopen(path, "rb");
    if (file == 0) {
        return -1;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    size = ftell(file);
    if (size <= 0) {
        fclose(file);
        return -1;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
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

static void print_plan(const struct RibonBootPlan *plan) {
    printf("ribon=%s\n", ribon_version_string());
    printf("firmware=%s\n", ribon_firmware_name(plan->firmware));
    printf("arch=%s\n", plan->arch->canonical_name);
    printf("arch-family=%s\n", plan->arch->family_name);
    printf("arch-tier=%s\n", ribon_arch_tier_name(plan->arch->tier));
    printf("arch-bits=%u\n", plan->arch->word_bits);
    printf("physical-address-bits=%u\n", plan->arch->physical_address_bits);
    printf("virtual-address-bits=%u\n", plan->arch->virtual_address_bits);
    printf("page-size=%llu\n", (unsigned long long)plan->arch->page_size);
    printf("large-page-size=%llu\n", (unsigned long long)plan->arch->large_page_size);
    printf("kernel-alignment=%llu\n", (unsigned long long)plan->arch->kernel_alignment);
    printf("handoff-alignment=%llu\n", (unsigned long long)plan->arch->handoff_alignment);
    printf("boot-module-alignment=%llu\n", (unsigned long long)plan->arch->boot_module_alignment);
    printf("arch-endian=%s\n", ribon_arch_endian_name(plan->arch->endian));
    printf("uefi-binding=%s\n", plan->arch->uefi_binding_dir);
    printf("environment-flags=0x%08x\n", plan->environment_flags);
    printf("memory-regions=%u\n", plan->memory_region_count);
    printf("normalized-memory-regions=%u\n", plan->normalized_memory_region_count);
    printf("usable-memory=0x%016llx\n", (unsigned long long)plan->usable_memory_bytes);
    printf("boot-media=%s\n", ribon_boot_media_name(plan->boot_media));
    printf("boot-modules=%u\n", plan->boot_module_count);
    printf("dtb=0x%llx:%llu\n",
           (unsigned long long)plan->device_tree_address,
           (unsigned long long)plan->device_tree_size);
    printf("framebuffer=0x%llx\n", (unsigned long long)plan->framebuffer_address);
    printf("cmdline=%s\n", plan->command_line != 0 ? plan->command_line : "");
    printf("profile=%s\n", plan->profile_name);
    printf("kernel=%s\n", plan->kernel_path);
    printf("handoff=%s\n", plan->handoff_name);
    printf("handoff-artifact-format=%s\n",
           plan->handoff_artifact_format != 0 ? plan->handoff_artifact_format : "");
    printf("handoff-artifact-size=%llu\n", (unsigned long long)plan->handoff_artifact_size);
    printf("handoff-artifact-sections=%u\n", plan->handoff_artifact_sections);
    printf("payload-source=%s\n", plan->kernel_source_name != 0 ? plan->kernel_source_name : "");
    printf("payload-format=%s\n", ribon_executable_format_name(plan->kernel_format));
    printf("kernel-machine=%u\n", plan->kernel_machine);
    printf("kernel-load-segments=%u\n", plan->kernel_load_segment_count);
    printf("kernel-load-plan-flags=0x%08x\n", plan->kernel_load_plan_flags);
    printf("kernel-entry=0x%016llx\n", (unsigned long long)plan->kernel_entry_point);
    printf("kernel-entry-load=0x%016llx\n", (unsigned long long)plan->kernel_entry_load_address);
    printf("kernel-runtime-entry=0x%016llx\n", (unsigned long long)plan->kernel_runtime_entry_address);
    printf("kernel-load-base=0x%016llx\n", (unsigned long long)plan->kernel_load_base);
    printf("kernel-load-end=0x%016llx\n", (unsigned long long)plan->kernel_load_end);
    printf("kernel-runtime-load-base=0x%016llx\n", (unsigned long long)plan->kernel_runtime_load_base);
    printf("kernel-runtime-load-end=0x%016llx\n", (unsigned long long)plan->kernel_runtime_load_end);
    printf("kernel-memory-size=0x%016llx\n", (unsigned long long)plan->kernel_memory_size);
    printf("kernel-linked-vaddr-base=0x%016llx\n", (unsigned long long)plan->kernel_linked_virtual_base);
    printf("kernel-linked-vaddr-end=0x%016llx\n", (unsigned long long)plan->kernel_linked_virtual_end);
    printf("kernel-linked-paddr-base=0x%016llx\n", (unsigned long long)plan->kernel_linked_physical_base);
    printf("kernel-linked-paddr-end=0x%016llx\n", (unsigned long long)plan->kernel_linked_physical_end);
    printf("kernel-high-entry=0x%016llx\n", (unsigned long long)plan->kernel_high_entry_virtual_address);
    printf("kernel-high-entry-load=0x%016llx\n", (unsigned long long)plan->kernel_high_entry_load_address);
    printf("handoff-kind=%s\n", ribon_handoff_name(plan->handoff));
    printf("handoff-version=%u\n", plan->handoff_major);
    printf("memory-map=%s\n", status_word((plan->expectations & RIBON_PROFILE_EXPECT_MEMORY_MAP) != 0u));
    printf("kernel-image-layout=%s\n",
           status_word((plan->expectations & RIBON_PROFILE_EXPECT_KERNEL_IMAGE_LAYOUT) != 0u));
    printf("direct-high-entry=%s\n",
           (plan->expectations & RIBON_PROFILE_ALLOW_DIRECT_HIGH_ENTRY) != 0u ? "allowed" : "disabled");
}

int main(int argc, char **argv) {
    const char *profile_name = "parus";
    const char *kernel_path = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc) {
            profile_name = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--kernel") == 0 && i + 1 < argc) {
            kernel_path = argv[++i];
            continue;
        }
        fprintf(stderr, "usage: %s [--profile name] [--kernel path]\n", argv[0]);
        return 2;
    }

    const struct RibonProfile *profile = ribon_find_builtin_profile(profile_name);
    if (profile == 0) {
        fprintf(stderr, "unknown profile: %s\n", profile_name);
        return 1;
    }
    if (kernel_path == 0) {
        kernel_path = profile->kernel_path;
    }

    const struct RibonFirmwareAdapter *adapter = ribon_firmware_host_adapter();
    struct RibonBootEnvironment environment = {0};
    struct HostPayloadBuffer kernel_buffer = {0};
    struct RibonLoadSegment kernel_segments[8];
    struct RibonLoadedPayload kernel_layout = {
        .segments = kernel_segments,
        .segment_capacity = (uint32_t)(sizeof(kernel_segments) / sizeof(kernel_segments[0])),
    };
    unsigned char handoff_buffer[4096];
    struct RibonHandoffArtifact handoff_artifact = {0};
    struct RibonMemoryRegion normalized_regions[16];
    struct RibonMutableMemoryMap normalized_memory_map = {
        .regions = normalized_regions,
        .region_count = 0,
        .capacity = (uint32_t)(sizeof(normalized_regions) / sizeof(normalized_regions[0])),
    };
    const int collect_status = adapter->collect(ribon_arch_selected(), &environment);
    if (collect_status != RIBON_FIRMWARE_STATUS_OK) {
        fprintf(stderr, "failed to collect boot environment: %d\n", collect_status);
        return 1;
    }
    if (read_host_payload(kernel_path, &kernel_buffer) != 0) {
        fprintf(stderr, "failed to read kernel payload: %s\n", kernel_path);
        return 1;
    }

    const struct RibonPayloadImage kernel_payload = {
        .data = kernel_buffer.data,
        .size = kernel_buffer.size,
        .source_name = kernel_path,
    };
    const struct RibonBootRequest request = {
        .environment = &environment,
        .profile = profile,
        .normalized_memory_map = &normalized_memory_map,
        .kernel_payload = &kernel_payload,
        .kernel_layout = &kernel_layout,
        .handoff_buffer = handoff_buffer,
        .handoff_buffer_capacity = sizeof(handoff_buffer),
        .handoff_artifact = &handoff_artifact,
    };
    struct RibonBootPlan plan = {0};
    const int status = ribon_build_plan(&request, &plan);
    if (status != RIBON_STATUS_OK) {
        fprintf(stderr, "failed to build boot plan: %d\n", status);
        free(kernel_buffer.data);
        return 1;
    }

    print_plan(&plan);
    free(kernel_buffer.data);
    return 0;
}
