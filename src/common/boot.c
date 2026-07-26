#include <Ribon/arch/entry.h>
#include <Ribon/boot/transfer.h>

/** @brief Registry가 exact kind와 operation table을 선택했는지 검사한다. */
static int boot_registry_contains_operations(
    const struct RibonPluginRegistry *registry,
    enum RibonPluginKind kind,
    const void *operations) {
    for (uint32_t index = 0; index < registry->plugin_count; ++index) {
        if (registry->plugins[index]->kind == kind &&
            registry->plugins[index]->operations == operations) {
            return 1;
        }
    }
    return 0;
}

/** @brief Loaded payload plan이 prepare에 필요한 필드를 가지는지 검사한다. */
static int boot_loaded_payload_is_ready(const struct RibonLoadedPayload *layout) {
    return layout != 0 &&
           layout->format != RIBON_EXECUTABLE_FORMAT_UNKNOWN &&
           layout->segment_count != 0u &&
           layout->segments != 0;
}

/** @brief Validated Core와 selected plugin operation으로 새 boot session을 만든다. */
int ribon_boot_session_initialize(
    struct RibonBootSession *out,
    const struct RibonCoreContext *core,
    const struct RibonServiceTable *services,
    const struct RibonArchOps *arch,
    const struct RibonBootProtocol *protocol,
    const struct RibonImageFormatOps *image_format) {
    if (out == 0 ||
        ribon_core_context_validate(core) != RIBON_CORE_STATUS_OK ||
        !ribon_service_table_is_valid(services) ||
        !ribon_arch_ops_are_valid(arch) ||
        !ribon_boot_protocol_is_valid(protocol) ||
        !ribon_image_format_ops_are_valid(image_format) ||
        !boot_registry_contains_operations(
            core->registry,
            RIBON_PLUGIN_KIND_ENVIRONMENT,
            services) ||
        !boot_registry_contains_operations(
            core->registry,
            RIBON_PLUGIN_KIND_ARCHITECTURE,
            arch) ||
        !boot_registry_contains_operations(
            core->registry,
            RIBON_PLUGIN_KIND_BOOT_PROTOCOL,
            protocol) ||
        !boot_registry_contains_operations(
            core->registry,
            RIBON_PLUGIN_KIND_IMAGE_FORMAT,
            image_format) ||
        (protocol->ops->select_image_formats() &
         RIBON_IMAGE_FORMAT_MASK(image_format->format)) == 0u) {
        if (out != 0) {
            *out = (struct RibonBootSession){0};
        }
        return RIBON_BOOT_STATUS_BAD_ARGUMENT;
    }
    *out = (struct RibonBootSession){
        .size = sizeof(*out),
        .abi_version = RIBON_CORE_ABI_VERSION,
        .state = RIBON_BOOT_SESSION_INITIALIZED,
        .core = core,
        .services = services,
        .arch = arch,
        .protocol = protocol,
        .image_format = image_format,
    };
    return RIBON_BOOT_STATUS_OK;
}

/** @brief Boot plan의 image-related field를 caller-owned layout에서 채운다. */
static void boot_copy_image_plan(
    struct RibonBootPlan *out,
    const struct RibonPayloadImage *image,
    const struct RibonLoadedPayload *layout) {
    out->kernel_source_name = image->source_name;
    out->kernel_format = layout->format;
    out->kernel_machine = layout->machine;
    out->kernel_load_segment_count = layout->segment_count;
    out->kernel_load_plan_flags = layout->load_plan_flags;
    out->kernel_entry_point = layout->entry_point;
    out->kernel_entry_load_address = layout->entry_load_address;
    out->kernel_runtime_entry_address = layout->runtime_entry_address;
    out->kernel_load_base = layout->load_base;
    out->kernel_load_end = layout->load_end;
    out->kernel_runtime_load_base = layout->runtime_load_base;
    out->kernel_runtime_load_end = layout->runtime_load_end;
    out->kernel_memory_size = layout->memory_size;
    out->kernel_linked_virtual_base = layout->linked_virtual_base;
    out->kernel_linked_virtual_end = layout->linked_virtual_end;
    out->kernel_linked_physical_base = layout->linked_physical_base;
    out->kernel_linked_physical_end = layout->linked_physical_end;
    out->kernel_high_entry_virtual_address = layout->high_entry_virtual_address;
    out->kernel_high_entry_load_address = layout->high_entry_load_address;
    out->kernel_load_segments = layout->segments;
}

/** @brief Input을 검증하고 durable state를 바꾸지 않는 immutable plan을 만든다. */
int ribon_boot_prepare(
    struct RibonBootSession *session,
    const struct RibonBootRequest *request,
    struct RibonBootPlan *out) {
    struct RibonBootPlan candidate = {0};
    int status;

    if (session == 0 ||
        session->size != sizeof(*session) ||
        session->abi_version != RIBON_CORE_ABI_VERSION ||
        session->state != RIBON_BOOT_SESSION_INITIALIZED ||
        request == 0 ||
        out == 0 ||
        request->environment == 0 ||
        request->normalized_memory_map == 0 ||
        request->kernel_payload == 0 ||
        request->kernel_layout == 0 ||
        request->handoff_buffer == 0 ||
        request->handoff_buffer_capacity == 0u ||
        request->handoff_artifact == 0) {
        return RIBON_BOOT_STATUS_BAD_ARGUMENT;
    }
    if (!ribon_boot_environment_is_valid(request->environment) ||
        request->environment->architecture !=
            session->arch->descriptor->id) {
        return RIBON_BOOT_STATUS_INCOMPLETE_ENVIRONMENT;
    }
    if ((session->protocol->supported_modes &
         RIBON_MODE_MASK(session->core->mode->mode)) == 0u) {
        return RIBON_BOOT_STATUS_UNSUPPORTED;
    }
    if (ribon_boot_protocol_has_expectation(
            session->protocol,
            RIBON_PROTOCOL_EXPECT_MEMORY_MAP) &&
        (request->environment->flags & RIBON_BOOT_ENV_HAS_MEMORY_MAP) == 0u) {
        return RIBON_BOOT_STATUS_INCOMPLETE_ENVIRONMENT;
    }
    if (request->kernel_payload->size >
        session->core->product->limits.max_input_bytes) {
        return RIBON_BOOT_STATUS_BUDGET_EXCEEDED;
    }

    status = ribon_memory_map_normalize(
        &request->environment->memory_map,
        request->normalized_memory_map);
    if (status != RIBON_MEMORY_STATUS_OK) {
        return RIBON_BOOT_STATUS_INCOMPLETE_ENVIRONMENT;
    }
    if (!boot_loaded_payload_is_ready(request->kernel_layout)) {
        status = session->image_format->analyze(
            request->kernel_payload,
            session->arch->descriptor,
            request->kernel_layout);
        if (status != RIBON_LOADER_STATUS_OK) {
            return RIBON_BOOT_STATUS_INVALID_PAYLOAD;
        }
    }
    if (session->arch->validate_payload(
            session->arch->descriptor,
            request->kernel_layout) != RIBON_ARCH_OPERATION_OK) {
        return RIBON_BOOT_STATUS_INVALID_PAYLOAD;
    }
    if (session->protocol->ops->select_entry_contract(
            session->arch->descriptor,
            &candidate.entry_contract) != RIBON_PROTOCOL_STATUS_OK ||
        candidate.entry_contract.required_entry_flags == 0u ||
        (candidate.entry_contract.required_entry_flags &
         ~candidate.entry_contract.supported_entry_flags) != 0u) {
        return RIBON_BOOT_STATUS_UNSUPPORTED;
    }

    candidate.environment = request->environment->kind;
    candidate.arch = session->arch->descriptor;
    candidate.environment_flags = request->environment->flags;
    candidate.memory_region_count = request->environment->memory_map.region_count;
    candidate.normalized_memory_region_count =
        request->normalized_memory_map->region_count;
    candidate.usable_memory_bytes = ribon_memory_map_usable_bytes(
        &(const struct RibonMemoryMap){
            .regions = request->normalized_memory_map->regions,
            .region_count = request->normalized_memory_map->region_count,
        });
    candidate.boot_media = request->environment->boot_media.kind;
    candidate.boot_module_count = request->environment->boot_modules.module_count;
    candidate.device_tree_address =
        request->environment->device_tree.physical_address;
    candidate.device_tree_size = request->environment->device_tree.size;
    candidate.framebuffer_address =
        request->environment->framebuffer.physical_address;
    candidate.command_line = request->environment->command_line.text;
    candidate.protocol_id = session->protocol->id;
    candidate.kernel_path = session->protocol->kernel_path;
    candidate.handoff_format = session->protocol->handoff_format;
    candidate.handoff_major = session->protocol->handoff_major;
    candidate.expectations = session->protocol->expectations;
    boot_copy_image_plan(
        &candidate,
        request->kernel_payload,
        request->kernel_layout);

    status = session->protocol->ops->prepare_handoff(
        &candidate,
        request->environment,
        request->normalized_memory_map,
        request->handoff_buffer,
        request->handoff_buffer_capacity,
        request->handoff_artifact);
    if (status != RIBON_PROTOCOL_HANDOFF_STATUS_OK ||
        request->handoff_artifact->size >
            session->core->product->limits.max_handoff_bytes) {
        return RIBON_BOOT_STATUS_INVALID_HANDOFF;
    }
    candidate.handoff_artifact_format = request->handoff_artifact->format;
    candidate.handoff_artifact_size = request->handoff_artifact->size;
    candidate.handoff_artifact_sections =
        request->handoff_artifact->section_count;
    *out = candidate;
    session->state = RIBON_BOOT_SESSION_PREPARED;
    return RIBON_BOOT_STATUS_OK;
}

/** @brief Prepared attempt의 durable metadata commit 경계를 전진시킨다. */
int ribon_boot_commit(struct RibonBootSession *session) {
    if (session == 0 || session->state != RIBON_BOOT_SESSION_PREPARED) {
        return RIBON_BOOT_STATUS_BAD_STATE;
    }
    session->state = RIBON_BOOT_SESSION_COMMITTED;
    return RIBON_BOOT_STATUS_OK;
}

/** @brief Environment service 종료와 final memory-map freeze 경계를 전진시킨다. */
int ribon_environment_quiesce(struct RibonBootSession *session) {
    if (session == 0 || session->state != RIBON_BOOT_SESSION_COMMITTED) {
        return RIBON_BOOT_STATUS_BAD_STATE;
    }
    session->state = RIBON_BOOT_SESSION_QUIESCED;
    return RIBON_BOOT_STATUS_OK;
}

/** @brief Entry contract를 적용해 OS로 제어를 넘긴다. */
_Noreturn void ribon_boot_transfer(
    struct RibonBootSession *session,
    const struct RibonBootPlan *plan,
    uint64_t handoff_address,
    uint64_t entry_flags,
    uint64_t bootstrap0) {
    if (session == 0 ||
        plan == 0 ||
        session->state != RIBON_BOOT_SESSION_QUIESCED ||
        (entry_flags & plan->entry_contract.required_entry_flags) !=
            plan->entry_contract.required_entry_flags ||
        (entry_flags & ~plan->entry_contract.supported_entry_flags) != 0u) {
        if (session != 0 && session->arch != 0 && session->arch->halt != 0) {
            session->arch->halt();
        }
        for (;;) {
        }
    }
    session->state = RIBON_BOOT_SESSION_TRANSFERRED;
    ribon_arch_enter_kernel(
        plan->kernel_runtime_entry_address,
        handoff_address,
        entry_flags,
        bootstrap0);
}
