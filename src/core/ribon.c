#include <Ribon/ribon.h>

const char *ribon_version_string(void) {
    return "0.1.0";
}

static int ribon_loaded_payload_is_ready(const struct RibonLoadedPayload *layout) {
    return layout != 0 &&
           layout->format != RIBON_EXECUTABLE_FORMAT_UNKNOWN &&
           layout->segment_count != 0u &&
           layout->segments != 0;
}

int ribon_build_plan(const struct RibonBootRequest *request, struct RibonBootPlan *out) {
    if (request == 0 || out == 0 || request->profile == 0 || request->environment == 0) {
        return RIBON_STATUS_BAD_ARGUMENT;
    }
    if (!ribon_profile_is_valid(request->profile)) {
        return RIBON_STATUS_UNSUPPORTED_PROFILE;
    }
    if (!ribon_boot_environment_is_valid(request->environment)) {
        return RIBON_STATUS_UNSUPPORTED_ARCH;
    }
    if ((request->profile->expectations & RIBON_PROFILE_EXPECT_MEMORY_MAP) != 0u &&
        (request->environment->flags & RIBON_BOOT_ENV_HAS_MEMORY_MAP) == 0u) {
        return RIBON_STATUS_INCOMPLETE_ENVIRONMENT;
    }
    if ((request->profile->expectations & RIBON_PROFILE_EXPECT_MEMORY_MAP) != 0u) {
        int memory_status;
        if (request->normalized_memory_map == 0) {
            return RIBON_STATUS_BAD_ARGUMENT;
        }
        memory_status = ribon_memory_map_normalize(
            &request->environment->memory_map,
            request->normalized_memory_map);
        if (memory_status != RIBON_MEMORY_STATUS_OK) {
            return RIBON_STATUS_INCOMPLETE_ENVIRONMENT;
        }
    }
    if ((request->profile->expectations & RIBON_PROFILE_EXPECT_KERNEL_IMAGE_LAYOUT) != 0u) {
        int loader_status;
        if (request->kernel_payload == 0 || request->kernel_layout == 0) {
            return RIBON_STATUS_BAD_ARGUMENT;
        }
        if (!ribon_loaded_payload_is_ready(request->kernel_layout)) {
            loader_status = ribon_loader_analyze(
                request->kernel_payload,
                request->environment->arch,
                request->kernel_layout);
            if (loader_status != RIBON_LOADER_STATUS_OK) {
                return RIBON_STATUS_INVALID_PAYLOAD;
            }
        }
    }

    out->firmware = request->environment->firmware;
    out->arch = request->environment->arch;
    out->environment_flags = request->environment->flags;
    out->memory_region_count = request->environment->memory_map.region_count;
    out->normalized_memory_region_count =
        request->normalized_memory_map != 0 ? request->normalized_memory_map->region_count : 0u;
    out->usable_memory_bytes =
        request->normalized_memory_map != 0 ?
            ribon_memory_map_usable_bytes(
                &(const struct RibonMemoryMap){
                    .regions = request->normalized_memory_map->regions,
                    .region_count = request->normalized_memory_map->region_count,
                }) :
            0u;
    out->boot_media = request->environment->boot_media.kind;
    out->boot_module_count = request->environment->boot_modules.module_count;
    out->device_tree_address = request->environment->device_tree.physical_address;
    out->device_tree_size = request->environment->device_tree.size;
    out->framebuffer_address = request->environment->framebuffer.physical_address;
    out->command_line = request->environment->command_line.text;
    out->profile_name = request->profile->name;
    out->kernel_path = request->profile->kernel_path;
    out->handoff_name =
        request->profile->handoff_name != 0 ? request->profile->handoff_name :
                                              ribon_handoff_name(request->profile->handoff);
    out->kernel_source_name = request->kernel_payload != 0 ? request->kernel_payload->source_name : 0;
    out->kernel_format =
        request->kernel_layout != 0 ? request->kernel_layout->format : RIBON_EXECUTABLE_FORMAT_UNKNOWN;
    out->kernel_machine = request->kernel_layout != 0 ? request->kernel_layout->machine : 0u;
    out->kernel_load_segment_count =
        request->kernel_layout != 0 ? request->kernel_layout->segment_count : 0u;
    out->kernel_entry_point = request->kernel_layout != 0 ? request->kernel_layout->entry_point : 0u;
    out->kernel_entry_load_address =
        request->kernel_layout != 0 ? request->kernel_layout->entry_load_address : 0u;
    out->kernel_runtime_entry_address =
        request->kernel_layout != 0 ? request->kernel_layout->runtime_entry_address : 0u;
    out->kernel_load_base = request->kernel_layout != 0 ? request->kernel_layout->load_base : 0u;
    out->kernel_load_end = request->kernel_layout != 0 ? request->kernel_layout->load_end : 0u;
    out->kernel_runtime_load_base =
        request->kernel_layout != 0 ? request->kernel_layout->runtime_load_base : 0u;
    out->kernel_runtime_load_end =
        request->kernel_layout != 0 ? request->kernel_layout->runtime_load_end : 0u;
    out->kernel_memory_size = request->kernel_layout != 0 ? request->kernel_layout->memory_size : 0u;
    out->kernel_linked_virtual_base =
        request->kernel_layout != 0 ? request->kernel_layout->linked_virtual_base : 0u;
    out->kernel_linked_virtual_end =
        request->kernel_layout != 0 ? request->kernel_layout->linked_virtual_end : 0u;
    out->kernel_linked_physical_base =
        request->kernel_layout != 0 ? request->kernel_layout->linked_physical_base : 0u;
    out->kernel_linked_physical_end =
        request->kernel_layout != 0 ? request->kernel_layout->linked_physical_end : 0u;
    out->kernel_high_entry_virtual_address =
        request->kernel_layout != 0 ? request->kernel_layout->high_entry_virtual_address : 0u;
    out->kernel_high_entry_load_address =
        request->kernel_layout != 0 ? request->kernel_layout->high_entry_load_address : 0u;
    out->kernel_load_segments =
        request->kernel_layout != 0 ? request->kernel_layout->segments : 0;
    out->kernel_load_plan_flags =
        request->kernel_layout != 0 ? request->kernel_layout->load_plan_flags : 0u;
    out->expectations = request->profile->expectations;
    out->handoff = request->profile->handoff;
    out->handoff_major = request->profile->handoff_major;
    if (request->profile->ops->build_handoff != 0) {
        int handoff_status;
        if (request->handoff_buffer == 0 || request->handoff_buffer_capacity == 0u ||
            request->handoff_artifact == 0) {
            return RIBON_STATUS_BAD_ARGUMENT;
        }
        handoff_status = request->profile->ops->build_handoff(
            out,
            request->environment,
            request->normalized_memory_map,
            request->handoff_buffer,
            request->handoff_buffer_capacity,
            request->handoff_artifact);
        if (handoff_status != RIBON_PROFILE_HANDOFF_STATUS_OK) {
            return RIBON_STATUS_INVALID_HANDOFF;
        }
        out->handoff_artifact_format = request->handoff_artifact->format;
        out->handoff_artifact_size = request->handoff_artifact->size;
        out->handoff_artifact_sections = request->handoff_artifact->section_count;
    }
    return RIBON_STATUS_OK;
}
