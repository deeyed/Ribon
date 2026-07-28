#include <Ribon/protocol/protocol.h>
#include <Ribon/plugin/descriptor.h>

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
        protocol->handoff_format == 0 ||
        protocol->handoff_major == 0u ||
        protocol->ops == 0 ||
        protocol->ops->size != sizeof(*protocol->ops) ||
        protocol->ops->abi_version != RIBON_BOOT_PROTOCOL_OPS_ABI_VERSION ||
        protocol->ops->match == 0 ||
        protocol->ops->validate_components == 0 ||
        protocol->ops->select_image_formats == 0 ||
        protocol->ops->prepare_handoff == 0 ||
        protocol->ops->prepare_entry_invocation == 0 ||
        protocol->ops->validate_confirmation == 0) {
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
int ribon_boot_protocol_validate_confirmation(
    const struct RibonBootProtocol *protocol,
    const struct RibonBootConfirmation *confirmation,
    const struct RibonBootConfirmationExpectation *expected) {
    if (!ribon_boot_protocol_is_valid(protocol)) {
        return RIBON_PROTOCOL_STATUS_UNSUPPORTED;
    }
    return protocol->ops->validate_confirmation(confirmation, expected);
}

/** @brief Boot Protocol plugin descriptor와 operation table을 함께 검사한다. */
int ribon_protocol_plugin_operations_are_valid(
    const struct RibonPluginDescriptor *descriptor) {
    const struct RibonBootProtocol *protocol;
    const uint64_t required =
        RIBON_CAP_BOOT_PROTOCOL |
        RIBON_CAP_HANDOFF |
        RIBON_CAP_ENTRY_CONTRACT |
        RIBON_CAP_BOOT_CONFIRMATION;
    if (descriptor == 0 ||
        descriptor->kind != RIBON_PLUGIN_KIND_BOOT_PROTOCOL ||
        descriptor->operations_size != sizeof(struct RibonBootProtocol) ||
        descriptor->operations_abi == 0u ||
        descriptor->provides != required) {
        return 0;
    }
    protocol = (const struct RibonBootProtocol *)descriptor->operations;
    return ribon_boot_protocol_is_valid(protocol) &&
           protocol->abi_version == descriptor->operations_abi &&
           protocol->supported_modes == descriptor->mode_mask;
}
