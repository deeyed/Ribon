#include <Ribon/arch/entry.h>
#include <Ribon/boot/transfer.h>

#define RIBON_BOOT_ATTEMPT_RECORD_SIZE 40u

struct RibonBootDeadline {
    uint64_t start_ticks;
    uint64_t limit_ticks;
    uint64_t absolute_ticks;
};

/** @brief Registry가 exact kind와 operation table을 선택했는지 검사한다. */
static int boot_registry_contains_operations(
    const struct RibonPluginRegistry *registry,
    enum RibonPluginKind kind,
    const void *operations) {
    for (uint32_t index = 0u; index < registry->plugin_count; ++index) {
        if (registry->plugins[index]->kind == kind &&
            registry->plugins[index]->operations == operations) {
            return 1;
        }
    }
    return 0;
}

/** @brief Service role의 one-authority descriptor를 allocation 없이 찾는다. */
static const struct RibonServiceDescriptor *boot_service_authority(
    const struct RibonServiceDirectory *directory,
    enum RibonServiceKind kind) {
    const struct RibonServiceDescriptor *found = 0;
    if (directory == 0) {
        return 0;
    }
    for (uint32_t index = 0u; index < directory->service_count; ++index) {
        const struct RibonServiceDescriptor *service = directory->services[index];
        if (service == 0 || service->kind != kind ||
            service->cardinality != RIBON_SERVICE_CARDINALITY_AUTHORITY) {
            continue;
        }
        if (found != 0) {
            return 0;
        }
        found = service;
    }
    return found;
}

/** @brief 실패를 terminal stage와 pointer-free receipt로 고정한다. */
static int boot_fail(
    struct RibonBootTransaction *transaction,
    enum RibonBootLifecycleStage stage,
    enum RibonBootFailureReason reason,
    const char *provider_id,
    int status) {
    if (transaction == 0) {
        return status;
    }
    transaction->receipt = (struct RibonBootFailureReceipt){
        .stage = stage,
        .reason = reason,
        .provider_id = provider_id,
        .consumed_input_bytes = transaction->consumed_input_bytes,
        .consumed_output_bytes = transaction->consumed_output_bytes,
        .consumed_components = transaction->consumed_components,
        .consumed_retries = transaction->consumed_retries,
    };
    transaction->stage = RIBON_BOOT_STAGE_FAILED;
    return status;
}

/** @brief Timer service가 제공하는 tick unit으로 deadline을 안전하게 계산한다. */
static int boot_deadline_begin(
    const struct RibonBootTransaction *transaction,
    uint64_t requested_ms,
    struct RibonBootDeadline *out) {
    const struct RibonMonotonicTimerServiceOperations *operations;
    uint64_t limit;
    uint64_t start;
    if (transaction == 0 || transaction->timer == 0 || out == 0 ||
        transaction->timer->operations_size != sizeof(*operations)) {
        return RIBON_BOOT_STATUS_MISSING_CAPABILITY;
    }
    operations = transaction->timer->operations;
    if (operations == 0 || operations->size != sizeof(*operations) ||
        operations->abi_version != RIBON_SERVICE_ABI_VERSION ||
        operations->frequency_hz == 0u || operations->now == 0 ||
        operations->now(operations->context, &start) != RIBON_SERVICE_STATUS_OK) {
        return RIBON_BOOT_STATUS_TIMEOUT;
    }
    if (requested_ms == 0u || requested_ms > UINT64_MAX / operations->frequency_hz) {
        limit = UINT64_MAX;
    } else {
        const uint64_t numerator = requested_ms * operations->frequency_hz;
        limit = numerator / 1000u + (numerator % 1000u != 0u ? 1u : 0u);
        if (limit == 0u) {
            limit = 1u;
        }
    }
    *out = (struct RibonBootDeadline){
        .start_ticks = start,
        .limit_ticks = limit,
        .absolute_ticks = start > UINT64_MAX - limit ? UINT64_MAX : start + limit,
    };
    return RIBON_BOOT_STATUS_OK;
}

/** @brief Monotonic service로 callback 실행 시간이 descriptor deadline 안인지 검사한다. */
static int boot_deadline_end(
    const struct RibonBootTransaction *transaction,
    const struct RibonBootDeadline *deadline) {
    const struct RibonMonotonicTimerServiceOperations *operations;
    uint64_t end;
    if (transaction == 0 || transaction->timer == 0 || deadline == 0) {
        return RIBON_BOOT_STATUS_BAD_ARGUMENT;
    }
    operations = transaction->timer->operations;
    if (operations == 0 || operations->now == 0 ||
        operations->now(operations->context, &end) != RIBON_SERVICE_STATUS_OK ||
        end < deadline->start_ticks || end - deadline->start_ticks > deadline->limit_ticks) {
        return RIBON_BOOT_STATUS_TIMEOUT;
    }
    return RIBON_BOOT_STATUS_OK;
}

/** @brief Loaded payload plan의 image field를 immutable plan에 복사한다. */
static void boot_copy_image_plan(
    struct RibonBootPlan *out,
    const struct RibonPayloadImage *image,
    const struct RibonValidatedImage *validated,
    const struct RibonDirectLoadPlan *layout) {
    out->kernel_source_name = image->source_name;
    out->kernel_image = *validated;
    out->kernel_direct_load_plan = layout;
    if (layout == 0) {
        return;
    }
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

/** @brief Relative image plan을 selected physical placement window에 재배치한다. */
static int boot_apply_relocatable_placement(
    const struct RibonBootTransaction *transaction,
    struct RibonDirectLoadPlan *layout) {
    const struct RibonServiceDescriptor *service;
    const struct RibonPayloadPlacementServiceOperations *placement;
    uint64_t window_end;

    if ((layout->load_plan_flags & RIBON_LOAD_PLAN_RELOCATABLE) == 0u) {
        return RIBON_BOOT_STATUS_OK;
    }
    service = boot_service_authority(
        transaction->core->services,
        RIBON_SERVICE_KIND_PAYLOAD_PLACEMENT);
    if (service == 0 ||
        !ribon_payload_placement_service_operations_are_valid(service)) {
        return RIBON_BOOT_STATUS_MISSING_CAPABILITY;
    }
    placement = service->operations;
    if (placement == 0 || placement->physical_base == 0u ||
        placement->physical_size == 0u ||
        placement->physical_base > UINT64_MAX - placement->physical_size) {
        return RIBON_BOOT_STATUS_INVALID_PAYLOAD;
    }
    window_end = placement->physical_base + placement->physical_size;
    for (uint32_t index = 0u; index < layout->segment_count; ++index) {
        struct RibonLoadSegment *segment = &layout->segments[index];
        uint64_t load_address;
        uint64_t load_end;
        uint64_t virtual_address;
        if (segment->load_address > UINT64_MAX - placement->physical_base ||
            segment->virtual_address > UINT64_MAX - placement->physical_base) {
            return RIBON_BOOT_STATUS_INVALID_PAYLOAD;
        }
        load_address = placement->physical_base + segment->load_address;
        virtual_address = placement->physical_base + segment->virtual_address;
        if (load_address < placement->physical_base ||
            load_address > UINT64_MAX - segment->memory_size ||
            (load_end = load_address + segment->memory_size) > window_end) {
            return RIBON_BOOT_STATUS_INVALID_PAYLOAD;
        }
        segment->physical_address = load_address;
        segment->load_address = load_address;
        segment->runtime_address = load_address;
        segment->virtual_address = virtual_address;
        if (segment->linked_physical_address >
            UINT64_MAX - placement->physical_base) {
            return RIBON_BOOT_STATUS_INVALID_PAYLOAD;
        }
        segment->linked_physical_address += placement->physical_base;
    }
    if (layout->entry_point > UINT64_MAX - placement->physical_base ||
        layout->entry_load_address > UINT64_MAX - placement->physical_base ||
        layout->runtime_entry_address > UINT64_MAX - placement->physical_base ||
        layout->load_base > UINT64_MAX - placement->physical_base ||
        layout->load_end > UINT64_MAX - placement->physical_base ||
        layout->runtime_load_base > UINT64_MAX - placement->physical_base ||
        layout->runtime_load_end > UINT64_MAX - placement->physical_base ||
        layout->linked_virtual_base > UINT64_MAX - placement->physical_base ||
        layout->linked_virtual_end > UINT64_MAX - placement->physical_base ||
        layout->linked_physical_base > UINT64_MAX - placement->physical_base ||
        layout->linked_physical_end > UINT64_MAX - placement->physical_base) {
        return RIBON_BOOT_STATUS_INVALID_PAYLOAD;
    }
    layout->entry_point += placement->physical_base;
    layout->entry_load_address += placement->physical_base;
    layout->runtime_entry_address += placement->physical_base;
    layout->load_base += placement->physical_base;
    layout->load_end += placement->physical_base;
    layout->runtime_load_base += placement->physical_base;
    layout->runtime_load_end += placement->physical_base;
    layout->linked_virtual_base += placement->physical_base;
    layout->linked_virtual_end += placement->physical_base;
    layout->linked_physical_base += placement->physical_base;
    layout->linked_physical_end += placement->physical_base;
    return RIBON_BOOT_STATUS_OK;
}

/** @brief Frozen payload와 environment에서 protocol handoff plan을 bounded하게 만든다. */
static int boot_prepare_protocol_plan(struct RibonBootTransaction *transaction) {
    struct RibonBootDeadline deadline;
    struct RibonBootPlan candidate = {0};
    int status;
    status = boot_deadline_begin(
        transaction,
        transaction->core->product->limits.operation_deadline_ms,
        &deadline);
    if (status != RIBON_BOOT_STATUS_OK) {
        return boot_fail(transaction, RIBON_BOOT_STAGE_PREPARE_PROTOCOL,
                         RIBON_BOOT_FAILURE_TIMEOUT, transaction->timer->id, status);
    }
    candidate.environment = transaction->environment.kind;
    candidate.arch = transaction->arch->descriptor;
    candidate.environment_flags = transaction->environment.flags;
    candidate.memory_region_count = transaction->environment.memory_map.region_count;
    candidate.normalized_memory_region_count =
        transaction->input.normalized_memory_map->region_count;
    candidate.usable_memory_bytes = ribon_memory_map_usable_bytes(
        &(const struct RibonMemoryMap){
            .regions = transaction->input.normalized_memory_map->regions,
            .region_count = transaction->input.normalized_memory_map->region_count,
        });
    candidate.boot_media = transaction->environment.boot_media.kind;
    candidate.boot_module_count = transaction->environment.boot_modules.module_count;
    candidate.device_tree_address = transaction->environment.device_tree.physical_address;
    candidate.device_tree_size = transaction->environment.device_tree.size;
    candidate.framebuffer_address = transaction->environment.framebuffer.physical_address;
    candidate.command_line = transaction->environment.command_line.text;
    candidate.protocol_id = transaction->protocol->id;
    candidate.kernel_path = transaction->protocol->kernel_path;
    candidate.handoff_format = transaction->protocol->handoff_format;
    candidate.handoff_major = transaction->protocol->handoff_major;
    candidate.expectations = transaction->protocol->expectations;
    boot_copy_image_plan(
        &candidate,
        &transaction->payload,
        &transaction->validated_image,
        transaction->input.direct_load_plan);
    if (transaction->protocol->terminal_execution ==
        RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY) {
        status = transaction->protocol->ops->prepare_handoff(
            &candidate,
            &transaction->environment,
            transaction->input.normalized_memory_map,
            transaction->input.handoff_buffer,
            transaction->input.handoff_buffer_capacity,
            transaction->input.handoff_artifact);
        if (status != RIBON_PROTOCOL_HANDOFF_STATUS_OK ||
            transaction->input.handoff_artifact->size >
                transaction->core->product->limits.max_handoff_bytes ||
            transaction->input.handoff_artifact->size >
                transaction->input.handoff_buffer_capacity ||
            transaction->consumed_output_bytes >
                transaction->core->product->limits.max_handoff_bytes -
                    transaction->input.handoff_artifact->size) {
            return boot_fail(transaction, RIBON_BOOT_STAGE_PREPARE_PROTOCOL,
                             RIBON_BOOT_FAILURE_BUDGET, transaction->protocol->id,
                             RIBON_BOOT_STATUS_BUDGET_EXCEEDED);
        }
        candidate.handoff_artifact_format = transaction->input.handoff_artifact->format;
        candidate.handoff_artifact_size = transaction->input.handoff_artifact->size;
        candidate.handoff_artifact_sections = transaction->input.handoff_artifact->section_count;
    }
    status = boot_deadline_end(transaction, &deadline);
    if (status != RIBON_BOOT_STATUS_OK) {
        return boot_fail(transaction, RIBON_BOOT_STAGE_PREPARE_PROTOCOL,
                         RIBON_BOOT_FAILURE_TIMEOUT, transaction->protocol->id, status);
    }
    status = transaction->protocol->ops->prepare_terminal(
        transaction->arch->descriptor,
        &candidate,
        &transaction->environment,
        transaction->protocol->terminal_execution ==
                RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY ?
            transaction->input.handoff_artifact : 0,
        &transaction->terminal_request);
    if (status != RIBON_PROTOCOL_STATUS_OK ||
        !ribon_terminal_request_is_valid(&transaction->terminal_request) ||
        transaction->terminal_request.kind != transaction->protocol->terminal_execution) {
        transaction->terminal_request = (struct RibonTerminalRequest){0};
        transaction->prepared_entry = (struct RibonPreparedEntry){0};
        return boot_fail(transaction, RIBON_BOOT_STAGE_PREPARE_PROTOCOL,
                         RIBON_BOOT_FAILURE_PROTOCOL, transaction->protocol->id,
                         RIBON_BOOT_STATUS_UNSUPPORTED);
    }
    if (transaction->terminal_request.kind == RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY &&
        transaction->arch->prepare_entry(
            transaction->arch->descriptor,
            &transaction->terminal_request.direct_entry,
            &transaction->prepared_entry) != RIBON_ARCH_OPERATION_OK) {
        transaction->terminal_request = (struct RibonTerminalRequest){0};
        transaction->prepared_entry = (struct RibonPreparedEntry){0};
        return boot_fail(transaction, RIBON_BOOT_STAGE_PREPARE_PROTOCOL,
                         RIBON_BOOT_FAILURE_PROTOCOL, transaction->protocol->id,
                         RIBON_BOOT_STATUS_UNSUPPORTED);
    }
    transaction->plan = candidate;
    transaction->consumed_output_bytes += candidate.handoff_artifact_size;
    transaction->stage = RIBON_BOOT_STAGE_PREPARE_PROTOCOL;
    return RIBON_BOOT_STATUS_OK;
}

/** @brief Validated Core와 selected operation으로 bounded transaction을 초기화한다. */
int ribon_boot_transaction_initialize(
    struct RibonBootTransaction *out,
    const struct RibonCoreContext *core,
    const struct RibonArchOps *arch,
    const struct RibonBootProtocol *protocol,
    const struct RibonImageFormatOps *image_format) {
    const struct RibonServiceDirectory *services;
    if (out == 0 || ribon_core_context_validate(core) != RIBON_CORE_STATUS_OK ||
        !ribon_arch_ops_are_valid(arch) || !ribon_boot_protocol_is_valid(protocol) ||
        !ribon_image_format_ops_are_valid(image_format) ||
        !boot_registry_contains_operations(core->registry, RIBON_PLUGIN_KIND_ARCHITECTURE, arch) ||
        !boot_registry_contains_operations(core->registry, RIBON_PLUGIN_KIND_BOOT_PROTOCOL, protocol) ||
        !boot_registry_contains_operations(core->registry, RIBON_PLUGIN_KIND_IMAGE_FORMAT, image_format) ||
        (protocol->ops->select_image_formats() &
         RIBON_IMAGE_FORMAT_MASK(image_format->format)) == 0u ||
        (protocol->terminal_execution == RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY &&
         (image_format->execution_support & RIBON_IMAGE_EXECUTION_DIRECT_ENTRY) == 0u) ||
        (protocol->terminal_execution ==
             RIBON_TERMINAL_EXECUTION_FIRMWARE_MANAGED_IMAGE &&
         (image_format->execution_support & RIBON_IMAGE_EXECUTION_FIRMWARE_MANAGED) == 0u)) {
        if (out != 0) {
            *out = (struct RibonBootTransaction){0};
        }
        return RIBON_BOOT_STATUS_BAD_ARGUMENT;
    }
    services = core->services;
    *out = (struct RibonBootTransaction){
        .size = sizeof(*out),
        .abi_version = RIBON_CORE_ABI_VERSION,
        .stage = RIBON_BOOT_STAGE_CAPTURE,
        .core = core,
        .arch = arch,
        .protocol = protocol,
        .image_format = image_format,
        .boot_source = boot_service_authority(services, RIBON_SERVICE_KIND_BOOT_SOURCE),
        .timer = boot_service_authority(services, RIBON_SERVICE_KIND_MONOTONIC_TIMER),
        .metadata = boot_service_authority(services, RIBON_SERVICE_KIND_PERSISTENT_METADATA),
        .flush = boot_service_authority(services, RIBON_SERVICE_KIND_STORAGE_FLUSH),
        .quiesce = boot_service_authority(services, RIBON_SERVICE_KIND_ENVIRONMENT_QUIESCE),
    };
    if (out->boot_source == 0 || out->timer == 0 || out->metadata == 0 ||
        out->flush == 0 || out->quiesce == 0) {
        *out = (struct RibonBootTransaction){0};
        return RIBON_BOOT_STATUS_MISSING_CAPABILITY;
    }
    return RIBON_BOOT_STATUS_OK;
}

/** @brief Source read부터 protocol handoff까지 immutable transaction을 전진시킨다. */
int ribon_boot_transaction_prepare(
    struct RibonBootTransaction *transaction,
    const struct RibonBootTransactionInput *input) {
    const struct RibonBootSourceServiceOperations *source_operations;
    struct RibonBootDeadline deadline;
    int status;
    if (transaction == 0 || transaction->size != sizeof(*transaction) ||
        transaction->abi_version != RIBON_CORE_ABI_VERSION ||
        transaction->stage != RIBON_BOOT_STAGE_CAPTURE || input == 0 ||
        input->environment == 0 || input->normalized_memory_map == 0 ||
        input->source == 0 || input->source_size == 0u || input->payload_buffer == 0 ||
        input->payload_buffer_capacity < input->source_size || input->source_name == 0 ||
        input->validated_image == 0 ||
        (transaction->protocol->terminal_execution == RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY &&
         (input->direct_load_plan == 0 || input->handoff_buffer == 0 ||
          input->handoff_buffer_capacity == 0u || input->handoff_artifact == 0)) ||
        (transaction->protocol->terminal_execution ==
             RIBON_TERMINAL_EXECUTION_FIRMWARE_MANAGED_IMAGE &&
         (input->direct_load_plan != 0 || input->handoff_buffer != 0 ||
          input->handoff_buffer_capacity != 0u || input->handoff_artifact != 0))) {
        return boot_fail(transaction, RIBON_BOOT_STAGE_CAPTURE, RIBON_BOOT_FAILURE_BAD_INPUT,
                         0, RIBON_BOOT_STATUS_BAD_ARGUMENT);
    }
    transaction->input = *input;
    transaction->stage = RIBON_BOOT_STAGE_CAPTURE;
    transaction->stage = RIBON_BOOT_STAGE_VALIDATE_PRODUCT;
    if (ribon_core_context_validate(transaction->core) != RIBON_CORE_STATUS_OK ||
        (transaction->protocol->supported_modes &
         RIBON_MODE_MASK(transaction->core->mode->mode)) == 0u) {
        return boot_fail(transaction, RIBON_BOOT_STAGE_VALIDATE_PRODUCT,
                         RIBON_BOOT_FAILURE_PRODUCT, 0, RIBON_BOOT_STATUS_UNSUPPORTED);
    }
    transaction->stage = RIBON_BOOT_STAGE_NORMALIZE_ENVIRONMENT;
    if (!ribon_boot_environment_is_valid(input->environment) ||
        input->environment->architecture != transaction->arch->descriptor->id ||
        (input->environment->boot_modules.module_count != 0u &&
         !ribon_boot_protocol_has_expectation(
             transaction->protocol, RIBON_PROTOCOL_ALLOW_BOOT_MODULES)) ||
        (ribon_boot_protocol_has_expectation(
             transaction->protocol, RIBON_PROTOCOL_EXPECT_MEMORY_MAP) &&
         (input->environment->flags & RIBON_BOOT_ENV_HAS_MEMORY_MAP) == 0u)) {
        return boot_fail(transaction, RIBON_BOOT_STAGE_NORMALIZE_ENVIRONMENT,
                         RIBON_BOOT_FAILURE_BAD_INPUT, 0,
                         RIBON_BOOT_STATUS_INCOMPLETE_ENVIRONMENT);
    }
    transaction->environment = *input->environment;
    if (ribon_memory_map_normalize(
            &transaction->environment.memory_map, input->normalized_memory_map) !=
        RIBON_MEMORY_STATUS_OK) {
        return boot_fail(transaction, RIBON_BOOT_STAGE_NORMALIZE_ENVIRONMENT,
                         RIBON_BOOT_FAILURE_BAD_INPUT, 0,
                         RIBON_BOOT_STATUS_INCOMPLETE_ENVIRONMENT);
    }
    transaction->stage = RIBON_BOOT_STAGE_SELECT_SOURCE;
    if (input->source->kind == RIBON_BOOT_MEDIA_NONE ||
        input->source_offset > input->source->size ||
        input->source_size > input->source->size - input->source_offset ||
        input->source_size > transaction->core->product->limits.max_input_bytes ||
        input->source_size > transaction->boot_source->input_budget) {
        return boot_fail(transaction, RIBON_BOOT_STAGE_SELECT_SOURCE,
                         RIBON_BOOT_FAILURE_BUDGET, transaction->boot_source->id,
                         RIBON_BOOT_STATUS_BUDGET_EXCEEDED);
    }
    transaction->source = *input->source;
    transaction->stage = RIBON_BOOT_STAGE_VERIFY_MANIFEST;
    if (input->source_name[0] == '\0') {
        return boot_fail(transaction, RIBON_BOOT_STAGE_VERIFY_MANIFEST,
                         RIBON_BOOT_FAILURE_BAD_INPUT, transaction->boot_source->id,
                         RIBON_BOOT_STATUS_BAD_ARGUMENT);
    }
    transaction->stage = RIBON_BOOT_STAGE_LOAD_IMAGE;
    source_operations = transaction->boot_source->operations;
    if (source_operations == 0 || source_operations->size != sizeof(*source_operations) ||
        source_operations->abi_version != RIBON_SERVICE_ABI_VERSION ||
        source_operations->read == 0 ||
        boot_deadline_begin(transaction, transaction->boot_source->deadline_ms, &deadline) !=
            RIBON_BOOT_STATUS_OK) {
        return boot_fail(transaction, RIBON_BOOT_STAGE_LOAD_IMAGE,
                         RIBON_BOOT_FAILURE_TIMEOUT, transaction->boot_source->id,
                         RIBON_BOOT_STATUS_TIMEOUT);
    }
    for (;;) {
        status = source_operations->read(
            source_operations->context,
            &transaction->source,
            input->source_offset,
            input->payload_buffer,
            input->source_size,
            deadline.absolute_ticks);
        if (status == RIBON_SERVICE_STATUS_OK ||
            transaction->consumed_retries >= transaction->core->product->limits.max_retries) {
            break;
        }
        ++transaction->consumed_retries;
    }
    if (status != RIBON_SERVICE_STATUS_OK) {
        return boot_fail(transaction, RIBON_BOOT_STAGE_LOAD_IMAGE,
                         RIBON_BOOT_FAILURE_SOURCE, transaction->boot_source->id,
                         RIBON_BOOT_STATUS_PROVIDER_FAILURE);
    }
    transaction->consumed_input_bytes += input->source_size;
    if (boot_deadline_end(transaction, &deadline) != RIBON_BOOT_STATUS_OK) {
        return boot_fail(transaction, RIBON_BOOT_STAGE_LOAD_IMAGE,
                         RIBON_BOOT_FAILURE_TIMEOUT, transaction->boot_source->id,
                         RIBON_BOOT_STATUS_TIMEOUT);
    }
    transaction->payload = (struct RibonPayloadImage){
        .data = input->payload_buffer,
        .size = input->source_size,
        .source_name = input->source_name,
    };
    if (transaction->image_format->analyze(
            &transaction->payload,
            input->validated_image,
            input->direct_load_plan) != RIBON_LOADER_STATUS_OK ||
        !ribon_validated_image_is_valid(input->validated_image) ||
        input->validated_image->format != transaction->image_format->format ||
        input->validated_image->image_size != transaction->payload.size ||
        (input->validated_image->execution_support &
         transaction->image_format->execution_support) !=
            input->validated_image->execution_support ||
        (transaction->protocol->terminal_execution ==
             RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY &&
         ((input->validated_image->execution_support &
           RIBON_IMAGE_EXECUTION_DIRECT_ENTRY) == 0u ||
          boot_apply_relocatable_placement(
              transaction, input->direct_load_plan) != RIBON_BOOT_STATUS_OK ||
          transaction->arch->validate_direct_load(
              transaction->arch->descriptor,
              input->validated_image,
              input->direct_load_plan) != RIBON_ARCH_OPERATION_OK)) ||
        (transaction->protocol->terminal_execution ==
             RIBON_TERMINAL_EXECUTION_FIRMWARE_MANAGED_IMAGE &&
         ((input->validated_image->execution_support &
           RIBON_IMAGE_EXECUTION_FIRMWARE_MANAGED) == 0u ||
          input->validated_image->machine !=
              transaction->arch->descriptor->pe_coff_machine))) {
        return boot_fail(transaction, RIBON_BOOT_STAGE_LOAD_IMAGE,
                         RIBON_BOOT_FAILURE_IMAGE,
                         ribon_executable_format_name(transaction->image_format->format),
                         RIBON_BOOT_STATUS_INVALID_PAYLOAD);
    }
    if (input->direct_load_plan != 0 &&
        (input->direct_load_plan->segment_count >
            transaction->core->product->limits.max_load_segments ||
        input->direct_load_plan->segment_count >
            transaction->core->product->limits.max_components ||
        transaction->environment.boot_modules.module_count >
            transaction->core->product->limits.max_components -
                input->direct_load_plan->segment_count)) {
        return boot_fail(transaction, RIBON_BOOT_STAGE_LOAD_IMAGE,
                         RIBON_BOOT_FAILURE_BUDGET,
                         ribon_executable_format_name(transaction->image_format->format),
                         RIBON_BOOT_STATUS_BUDGET_EXCEEDED);
    }
    transaction->consumed_components =
        (input->direct_load_plan != 0 ? input->direct_load_plan->segment_count : 1u) +
        transaction->environment.boot_modules.module_count;
    transaction->validated_image = *input->validated_image;
    return boot_prepare_protocol_plan(transaction);
}

/** @brief Attempt record를 byte-wise little-endian으로 caller-owned metadata service에 기록한다. */
static void boot_write_u64(unsigned char *buffer, uint32_t offset, uint64_t value) {
    for (uint32_t index = 0u; index < 8u; ++index) {
        buffer[offset + index] = (unsigned char)(value >> (index * 8u));
    }
}

/** @brief Prepared attempt를 metadata write와 flush 뒤 durable하게 commit한다. */
int ribon_boot_transaction_commit_attempt(struct RibonBootTransaction *transaction) {
    const struct RibonPersistentMetadataServiceOperations *metadata;
    const struct RibonStorageFlushServiceOperations *flush;
    struct RibonBootDeadline deadline;
    unsigned char record[RIBON_BOOT_ATTEMPT_RECORD_SIZE] = {0};
    int status;
    if (transaction == 0 || transaction->stage != RIBON_BOOT_STAGE_PREPARE_PROTOCOL) {
        return boot_fail(transaction, RIBON_BOOT_STAGE_COMMIT_ATTEMPT,
                         RIBON_BOOT_FAILURE_BAD_INPUT, 0, RIBON_BOOT_STATUS_BAD_STATE);
    }
    metadata = transaction->metadata->operations;
    flush = transaction->flush->operations;
    if (metadata == 0 || flush == 0 || metadata->size != sizeof(*metadata) ||
        flush->size != sizeof(*flush) ||
        metadata->abi_version != RIBON_SERVICE_ABI_VERSION ||
        flush->abi_version != RIBON_SERVICE_ABI_VERSION || metadata->write == 0 ||
        metadata->read == 0 || flush->flush == 0 ||
        boot_deadline_begin(transaction, transaction->metadata->deadline_ms, &deadline) !=
            RIBON_BOOT_STATUS_OK) {
        return boot_fail(transaction, RIBON_BOOT_STAGE_COMMIT_ATTEMPT,
                         RIBON_BOOT_FAILURE_COMMIT, transaction->metadata->id,
                         RIBON_BOOT_STATUS_PROVIDER_FAILURE);
    }
    record[0] = 'R'; record[1] = 'B'; record[2] = 'A'; record[3] = '1';
    record[4] = (unsigned char)RIBON_BOOT_STAGE_COMMIT_ATTEMPT;
    record[5] = (unsigned char)transaction->protocol->handoff_major;
    boot_write_u64(record, 8u, transaction->consumed_input_bytes);
    boot_write_u64(record, 16u, transaction->plan.handoff_artifact_size);
    boot_write_u64(
        record,
        24u,
        transaction->plan.kernel_direct_load_plan != 0 ?
            transaction->plan.kernel_entry_load_address : 0u);
    boot_write_u64(
        record,
        32u,
        transaction->plan.kernel_direct_load_plan != 0 ?
            transaction->plan.kernel_runtime_entry_address : 0u);
    status = metadata->write(metadata->context, 0u, record, sizeof(record));
    if (status != RIBON_SERVICE_STATUS_OK ||
        flush->flush(flush->context, 0u, deadline.absolute_ticks) != RIBON_SERVICE_STATUS_OK ||
        boot_deadline_end(transaction, &deadline) != RIBON_BOOT_STATUS_OK) {
        return boot_fail(transaction, RIBON_BOOT_STAGE_COMMIT_ATTEMPT,
                         RIBON_BOOT_FAILURE_COMMIT, transaction->metadata->id,
                         RIBON_BOOT_STATUS_PROVIDER_FAILURE);
    }
    transaction->stage = RIBON_BOOT_STAGE_COMMIT_ATTEMPT;
    return RIBON_BOOT_STATUS_OK;
}

/** @brief Commit 뒤 final environment fact로 source 선택 없이 handoff plan만 갱신한다. */
int ribon_boot_transaction_refresh_after_commit(
    struct RibonBootTransaction *transaction,
    const struct RibonBootEnvironment *environment) {
    if (transaction == 0 || transaction->stage != RIBON_BOOT_STAGE_COMMIT_ATTEMPT ||
        transaction->terminal_request.kind != RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY ||
        !ribon_boot_environment_is_valid(environment) ||
        environment->architecture != transaction->arch->descriptor->id ||
        ribon_memory_map_normalize(&environment->memory_map,
                                   transaction->input.normalized_memory_map) !=
            RIBON_MEMORY_STATUS_OK) {
        return boot_fail(transaction, RIBON_BOOT_STAGE_COMMIT_ATTEMPT,
                         RIBON_BOOT_FAILURE_BAD_INPUT, 0,
                         RIBON_BOOT_STATUS_INCOMPLETE_ENVIRONMENT);
    }
    transaction->environment = *environment;
    {
        const int status = boot_prepare_protocol_plan(transaction);
        if (status != RIBON_BOOT_STATUS_OK) {
            return status;
        }
    }
    transaction->stage = RIBON_BOOT_STAGE_COMMIT_ATTEMPT;
    return RIBON_BOOT_STATUS_OK;
}

/** @brief Selected environment closure operation을 실행하고 service lifetime을 닫는다. */
int ribon_boot_transaction_quiesce_environment(
    struct RibonBootTransaction *transaction) {
    const struct RibonEnvironmentQuiesceServiceOperations *operations;
    int status;
    if (transaction == 0 || transaction->stage != RIBON_BOOT_STAGE_COMMIT_ATTEMPT) {
        return boot_fail(transaction, RIBON_BOOT_STAGE_QUIESCE_ENVIRONMENT,
                         RIBON_BOOT_FAILURE_BAD_INPUT, 0, RIBON_BOOT_STATUS_BAD_STATE);
    }
    if (transaction->terminal_request.kind != RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY) {
        return boot_fail(transaction, RIBON_BOOT_STAGE_EXECUTE_TERMINAL,
                         RIBON_BOOT_FAILURE_TERMINAL, transaction->protocol->id,
                         RIBON_BOOT_STATUS_UNSUPPORTED);
    }
    operations = transaction->quiesce->operations;
    if (operations == 0 || operations->size != sizeof(*operations) ||
        operations->abi_version != RIBON_SERVICE_ABI_VERSION || operations->quiesce == 0) {
        return boot_fail(transaction, RIBON_BOOT_STAGE_QUIESCE_ENVIRONMENT,
                         RIBON_BOOT_FAILURE_QUIESCE, transaction->quiesce->id,
                         RIBON_BOOT_STATUS_PROVIDER_FAILURE);
    }
    status = operations->quiesce(operations->context);
    if (status != RIBON_SERVICE_STATUS_OK) {
        return boot_fail(transaction, RIBON_BOOT_STAGE_QUIESCE_ENVIRONMENT,
                         RIBON_BOOT_FAILURE_QUIESCE, transaction->quiesce->id,
                         RIBON_BOOT_STATUS_PROVIDER_FAILURE);
    }
    transaction->stage = RIBON_BOOT_STAGE_QUIESCE_ENVIRONMENT;
    return RIBON_BOOT_STATUS_OK;
}

/** @brief Prepared plan의 borrowed immutable view를 반환한다. */
const struct RibonBootPlan *ribon_boot_transaction_plan(
    const struct RibonBootTransaction *transaction) {
    return transaction != 0 && transaction->stage >= RIBON_BOOT_STAGE_PREPARE_PROTOCOL &&
           transaction->stage != RIBON_BOOT_STAGE_FAILED ? &transaction->plan : 0;
}

/** @brief Terminal failure receipt의 borrowed immutable view를 반환한다. */
const struct RibonBootFailureReceipt *ribon_boot_transaction_failure_receipt(
    const struct RibonBootTransaction *transaction) {
    return transaction != 0 && transaction->stage == RIBON_BOOT_STAGE_FAILED ?
        &transaction->receipt : 0;
}

/** @brief Sealed protocol invocation을 적용해 OS로 제어를 넘기고 반환하지 않는다. */
_Noreturn void ribon_boot_transaction_transfer(
    struct RibonBootTransaction *transaction) {
    if (transaction == 0 ||
        transaction->stage != RIBON_BOOT_STAGE_QUIESCE_ENVIRONMENT ||
        transaction->terminal_request.kind != RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY ||
        transaction->prepared_entry.invocation.size !=
            sizeof(transaction->prepared_entry.invocation)) {
        if (transaction != 0 && transaction->arch != 0 && transaction->arch->halt != 0) {
            transaction->arch->halt();
        }
        for (;;) {
        }
    }
    transaction->stage = RIBON_BOOT_STAGE_TRANSFER;
    transaction->arch->transfer_prepared(&transaction->prepared_entry);
    transaction->arch->halt();
    for (;;) {
    }
}
