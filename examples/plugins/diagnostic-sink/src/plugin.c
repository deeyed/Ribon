#include <Ribon/core/status.h>
#include <RibonExamples/diagnostic_sink.h>

/** @brief Overflow와 caller budget을 넘지 않는 write만 승인한다. */
static int diagnostic_sink_write(
    struct RibonExampleDiagnosticSink *sink,
    const void *bytes,
    uint64_t size) {
    if (sink == 0 || bytes == 0 || size == 0u ||
        sink->bytes_written > sink->byte_limit ||
        size > sink->byte_limit - sink->bytes_written) {
        return RIBON_CORE_STATUS_BAD_LIMIT;
    }
    sink->bytes_written += size;
    return RIBON_CORE_STATUS_OK;
}

static const struct RibonExampleDiagnosticSinkOperations sink_operations = {
    .size = sizeof(sink_operations),
    .abi_version = RIBON_EXAMPLE_DIAGNOSTIC_SINK_ABI_VERSION,
    .write = diagnostic_sink_write,
};

/** @brief Example service operation table의 size, ABI와 callback을 검사한다. */
static int diagnostic_sink_operations_are_valid(
    const struct RibonPluginDescriptor *descriptor) {
    const struct RibonExampleDiagnosticSinkOperations *operations;

    if (descriptor == 0 ||
        descriptor->kind != RIBON_PLUGIN_KIND_SERVICE ||
        descriptor->operations == 0 ||
        descriptor->operations_size != sizeof(*operations) ||
        descriptor->operations_abi !=
            RIBON_EXAMPLE_DIAGNOSTIC_SINK_ABI_VERSION) {
        return 0;
    }
    operations = descriptor->operations;
    return operations->size == sizeof(*operations) &&
           operations->abi_version ==
               RIBON_EXAMPLE_DIAGNOSTIC_SINK_ABI_VERSION &&
           operations->write != 0;
}

/** @brief External package가 제공하는 bounded diagnostic service plugin이다. */
const struct RibonPluginDescriptor
    ribon_example_diagnostic_sink_plugin_descriptor = {
        .magic = RIBON_PLUGIN_DESCRIPTOR_MAGIC,
        .size = sizeof(ribon_example_diagnostic_sink_plugin_descriptor),
        .abi_major = RIBON_PLUGIN_ABI_MAJOR,
        .abi_minor = RIBON_PLUGIN_ABI_MINOR,
        .kind = RIBON_PLUGIN_KIND_SERVICE,
        .phase = RIBON_PLUGIN_PHASE_BOOT,
        .id = "service.diagnostic-sink",
        .provides =
            RIBON_CAP_DIAGNOSTIC_SINK |
            RIBON_CAP_SDK_CONTRACT,
        .architecture_mask = RIBON_ARCH_MASK_X86_64,
        .environment_mask = RIBON_ENV_MASK_HOST,
        .mode_mask = RIBON_MODE_MASK(RIBON_MODE_DIAGNOSTIC),
        .arena_budget = 1024u,
        .input_budget = 4096u,
        .output_budget = 4096u,
        .deadline_ms = 1000u,
        .operations = &sink_operations,
        .operations_size = sizeof(sink_operations),
        .operations_abi = RIBON_EXAMPLE_DIAGNOSTIC_SINK_ABI_VERSION,
        .validate_operations = diagnostic_sink_operations_are_valid,
    };

/** @brief Source package manifest와 compiled descriptor를 연결한다. */
const struct RibonSdkPluginPackage ribon_example_diagnostic_sink_package = {
    .magic = RIBON_SDK_PLUGIN_PACKAGE_MAGIC,
    .size = sizeof(ribon_example_diagnostic_sink_package),
    .sdk_abi_version = RIBON_SDK_ABI_VERSION,
    .package_id = "example.diagnostic-sink",
    .contract_id = "ribon.example.diagnostic-sink.v1",
    .plugin = &ribon_example_diagnostic_sink_plugin_descriptor,
};
