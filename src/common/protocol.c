#include <Ribon/protocol/protocol.h>
#include <Ribon/plugin/descriptor.h>

/** @brief Entry invocation이 비어 있는지 allocation 없이 검사한다. */
static int terminal_direct_entry_is_zero(const struct RibonEntryInvocation *entry) {
    if (entry->size != 0u || entry->abi_version != 0u || entry->entry_address != 0u ||
        entry->register_abi != 0 || entry->argument_count != 0u ||
        entry->interrupts != 0 || entry->privilege != 0 || entry->translation != 0) {
        return 0;
    }
    for (uint32_t index = 0u; index < RIBON_ENTRY_ARGUMENT_LIMIT; ++index) {
        if (entry->arguments[index] != 0u) {
            return 0;
        }
    }
    return 1;
}

/** @brief Managed image option descriptor가 완전히 비어 있는지 검사한다. */
static int terminal_managed_image_is_zero(
    const struct RibonFirmwareManagedImageRequest *managed) {
    return managed->load_options_kind == RIBON_TERMINAL_LOAD_OPTIONS_NONE &&
           managed->load_options_size == 0u && managed->load_options == 0 &&
           managed->watchdog_timeout_ms == 0u;
}

/** @brief Terminal request의 kind별 불변식을 검사한다. */
int ribon_terminal_request_is_valid(const struct RibonTerminalRequest *request) {
    if (request == 0 || request->size != sizeof(*request) ||
        request->abi_version != RIBON_TERMINAL_REQUEST_ABI_VERSION ||
        request->reserved != 0u) {
        return 0;
    }
    if (request->kind == RIBON_TERMINAL_EXECUTION_FIRMWARE_MANAGED_IMAGE) {
        return terminal_direct_entry_is_zero(&request->direct_entry) &&
               request->managed_image.load_options_kind >=
                   RIBON_TERMINAL_LOAD_OPTIONS_NONE &&
               request->managed_image.load_options_kind <=
                   RIBON_TERMINAL_LOAD_OPTIONS_UTF8_COMMAND_LINE &&
               request->managed_image.watchdog_timeout_ms != 0u &&
               ((request->managed_image.load_options_kind ==
                     RIBON_TERMINAL_LOAD_OPTIONS_NONE &&
                 request->managed_image.load_options_size == 0u &&
                 request->managed_image.load_options == 0) ||
                (request->managed_image.load_options_kind ==
                     RIBON_TERMINAL_LOAD_OPTIONS_UTF8_COMMAND_LINE &&
                 request->managed_image.load_options_size != 0u &&
                 request->managed_image.load_options != 0));
    }
    return request->kind == RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY &&
           terminal_managed_image_is_zero(&request->managed_image) &&
           request->direct_entry.size == sizeof(request->direct_entry) &&
           request->direct_entry.abi_version == RIBON_ENTRY_INVOCATION_ABI_VERSION &&
           request->direct_entry.entry_address != 0u &&
           request->direct_entry.argument_count <= RIBON_ENTRY_ARGUMENT_LIMIT;
}

/** @brief Protocol descriptor와 callback 완전성을 검사한다. */
int ribon_boot_protocol_is_valid(const struct RibonBootProtocol *protocol) {
    uint64_t formats;
    if (protocol == 0 ||
        protocol->size != sizeof(*protocol) ||
        protocol->abi_version == 0u ||
        protocol->id == 0 ||
        protocol->id[0] == '\0' ||
        protocol->kernel_path == 0 ||
        protocol->supported_modes == 0u ||
        (protocol->supported_modes & ~RIBON_MODE_MASK_ALL) != 0u ||
        (protocol->terminal_execution != RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY &&
         protocol->terminal_execution != RIBON_TERMINAL_EXECUTION_FIRMWARE_MANAGED_IMAGE) ||
        protocol->ops == 0 ||
        protocol->ops->size != sizeof(*protocol->ops) ||
        protocol->ops->abi_version != RIBON_BOOT_PROTOCOL_OPS_ABI_VERSION ||
        protocol->ops->match == 0 ||
        protocol->ops->validate_components == 0 ||
        protocol->ops->select_image_formats == 0 ||
        protocol->ops->prepare_terminal == 0 ||
        protocol->ops->validate_boot_health == 0) {
        return 0;
    }
    if (protocol->terminal_execution == RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY) {
        if (protocol->handoff_format == 0 || protocol->handoff_major == 0u ||
            protocol->ops->prepare_handoff == 0) {
            return 0;
        }
    } else if (protocol->handoff_format != 0 || protocol->handoff_major != 0u ||
               protocol->ops->prepare_handoff != 0) {
        return 0;
    }
    formats = protocol->ops->select_image_formats();
    return formats != 0u &&
           (formats & RIBON_IMAGE_FORMAT_MASK(RIBON_EXECUTABLE_FORMAT_UNKNOWN)) == 0u;
}

/** @brief Protocol expectation bit를 모두 가지는지 검사한다. */
int ribon_boot_protocol_has_expectation(
    const struct RibonBootProtocol *protocol,
    uint32_t expectation) {
    return ribon_boot_protocol_is_valid(protocol) &&
           (protocol->expectations & expectation) == expectation;
}

/** @brief Protocol confirmation callback을 fail-closed로 호출한다. */
int ribon_boot_protocol_validate_boot_health(
    const struct RibonBootProtocol *protocol,
    const struct RibonBootHealthPayload *payload) {
    if (!ribon_boot_protocol_is_valid(protocol)) {
        return RIBON_PROTOCOL_STATUS_UNSUPPORTED;
    }
    return protocol->ops->validate_boot_health(payload);
}

/** @brief Boot Protocol plugin descriptor와 operation table을 함께 검사한다. */
int ribon_protocol_plugin_operations_are_valid(
    const struct RibonPluginDescriptor *descriptor) {
    const struct RibonBootProtocol *protocol;
    uint64_t required = RIBON_CAP_BOOT_PROTOCOL | RIBON_CAP_BOOT_CONFIRMATION;
    if (descriptor == 0 ||
        descriptor->kind != RIBON_PLUGIN_KIND_BOOT_PROTOCOL ||
        descriptor->operations_size != sizeof(struct RibonBootProtocol) ||
        descriptor->operations_abi == 0u ||
        descriptor->operations == 0) {
        return 0;
    }
    protocol = (const struct RibonBootProtocol *)descriptor->operations;
    if (protocol->terminal_execution == RIBON_TERMINAL_EXECUTION_DIRECT_ENTRY) {
        required |= RIBON_CAP_HANDOFF | RIBON_CAP_ENTRY_CONTRACT;
    }
    return ribon_boot_protocol_is_valid(protocol) &&
           descriptor->provides == required &&
           protocol->abi_version == descriptor->operations_abi &&
           protocol->supported_modes == descriptor->mode_mask;
}
