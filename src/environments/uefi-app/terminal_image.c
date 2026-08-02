#include "terminal_image.h"

#include "uefi_app.h"

#include <Protocol/DevicePath.h>
#include <Protocol/LoadedImage.h>

#define RIBON_UEFI_DEVICE_PATH_CAPACITY 4096u
#define RIBON_UEFI_LOAD_OPTIONS_CAPACITY 2048u
#define RIBON_UEFI_DEVICE_PATH_NODE_LIMIT 64u

static unsigned char terminal_device_path[RIBON_UEFI_DEVICE_PATH_CAPACITY];
static CHAR16 terminal_load_options[RIBON_UEFI_LOAD_OPTIONS_CAPACITY];

/** @brief Device-path node의 little-endian length를 읽는다. */
static uint16_t terminal_node_length(const EFI_DEVICE_PATH_PROTOCOL *node) {
    return (uint16_t)node->Length[0] | ((uint16_t)node->Length[1] << 8u);
}

/** @brief Device-path node header를 deterministic byte order로 기록한다. */
static void terminal_set_node(
    EFI_DEVICE_PATH_PROTOCOL *node,
    UINT8 type,
    UINT8 subtype,
    uint16_t size) {
    node->Type = type;
    node->SubType = subtype;
    node->Length[0] = (UINT8)size;
    node->Length[1] = (UINT8)(size >> 8u);
}

/** @brief Parent device path와 canonical file path를 one full path로 조합한다. */
static int terminal_build_device_path(
    struct RibonUefiAppContext *context,
    const char *path,
    unsigned char output[RIBON_UEFI_DEVICE_PATH_CAPACITY]) {
    EFI_GUID guid = EFI_DEVICE_PATH_PROTOCOL_GUID;
    EFI_DEVICE_PATH_PROTOCOL *base = 0;
    uint32_t base_size = 0u;
    uint32_t node_count = 0u;
    uint32_t path_length = 0u;
    uint32_t file_size;
    EFI_DEVICE_PATH_PROTOCOL *file_node;
    CHAR16 *native_path;
    EFI_DEVICE_PATH_PROTOCOL *end;
    if (context == 0 || context->boot_services == 0 || path == 0 || output == 0 ||
        context->device_handle == 0 ||
        context->boot_services->HandleProtocol == 0 ||
        EFI_ERROR(context->boot_services->HandleProtocol(
            context->device_handle, &guid, (void **)&base)) || base == 0) {
        return 0;
    }
    for (;;) {
        const uint16_t size = terminal_node_length(base);
        if (size < sizeof(*base) || ++node_count > RIBON_UEFI_DEVICE_PATH_NODE_LIMIT ||
            base_size > RIBON_UEFI_DEVICE_PATH_CAPACITY - size) {
            return 0;
        }
        if (base->Type == END_DEVICE_PATH_TYPE) {
            if (base->SubType != END_ENTIRE_DEVICE_PATH_SUBTYPE) {
                return 0;
            }
            break;
        }
        for (uint16_t index = 0u; index < size; ++index) {
            output[base_size + index] = ((const unsigned char *)base)[index];
        }
        base_size += size;
        base = (EFI_DEVICE_PATH_PROTOCOL *)((unsigned char *)base + size);
    }
    if (path[0] != '/' || path[1] == '\0') {
        return 0;
    }
    while (path[path_length] != '\0') {
        const unsigned char byte = (unsigned char)path[path_length];
        if (path_length + 1u >= RIBON_UEFI_PATH_CAPACITY ||
            byte < 0x21u || byte > 0x7eu || byte == '\\') {
            return 0;
        }
        ++path_length;
    }
    file_size = (uint32_t)sizeof(EFI_DEVICE_PATH_PROTOCOL) +
        (path_length + 1u) * (uint32_t)sizeof(CHAR16);
    if (file_size > UINT16_MAX || base_size >
        RIBON_UEFI_DEVICE_PATH_CAPACITY - file_size - sizeof(*end)) {
        return 0;
    }
    file_node = (EFI_DEVICE_PATH_PROTOCOL *)(output + base_size);
    terminal_set_node(file_node, MEDIA_DEVICE_PATH, MEDIA_FILEPATH_DP,
                      (uint16_t)file_size);
    native_path = (CHAR16 *)(output + base_size + sizeof(*file_node));
    for (uint32_t index = 0u; index < path_length; ++index) {
        native_path[index] = path[index] == '/' ? (CHAR16)'\\' : (CHAR16)path[index];
    }
    native_path[path_length] = 0u;
    end = (EFI_DEVICE_PATH_PROTOCOL *)(output + base_size + file_size);
    terminal_set_node(end, END_DEVICE_PATH_TYPE,
                      END_ENTIRE_DEVICE_PATH_SUBTYPE, (uint16_t)sizeof(*end));
    return 1;
}

/** @brief UTF-8 command-line subset을 bounded UEFI UTF-16 load options로 변환한다. */
static int terminal_build_load_options(
    const struct RibonTerminalImageLaunchRequest *request,
    CHAR16 output[RIBON_UEFI_LOAD_OPTIONS_CAPACITY],
    UINT32 *size_out) {
    const unsigned char *bytes = request->load_options;
    if (request->load_options_kind == RIBON_TERMINAL_LOAD_OPTIONS_NONE) {
        *size_out = 0u;
        return request->load_options == 0 && request->load_options_size == 0u;
    }
    if (request->load_options_kind != RIBON_TERMINAL_LOAD_OPTIONS_UTF8_COMMAND_LINE ||
        bytes == 0 || request->load_options_size == 0u ||
        request->load_options_size >= RIBON_UEFI_LOAD_OPTIONS_CAPACITY) {
        return 0;
    }
    for (uint32_t index = 0u; index < request->load_options_size; ++index) {
        if (bytes[index] < 0x20u || bytes[index] > 0x7eu) {
            return 0;
        }
        output[index] = (CHAR16)bytes[index];
    }
    output[request->load_options_size] = 0u;
    *size_out = (request->load_options_size + 1u) * (UINT32)sizeof(CHAR16);
    return 1;
}

/** @brief Exact source identity를 LoadImage/StartImage로 넘기고 반환을 실패로 봉인한다. */
static int terminal_launch(
    void *opaque,
    const struct RibonTerminalImageLaunchRequest *request,
    struct RibonTerminalImageLaunchReceipt *receipt) {
    struct RibonUefiAppContext *context = ribon_uefi_app_current_context();
    EFI_GUID loaded_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *loaded = 0;
    EFI_HANDLE child = 0;
    CHAR16 *exit_data = 0;
    UINTN exit_data_size = 0u;
    UINT32 load_options_size = 0u;
    EFI_STATUS status;
    UINTN watchdog_seconds;
    if (receipt != 0) {
        *receipt = (struct RibonTerminalImageLaunchReceipt){
            .size = sizeof(*receipt),
            .abi_version = RIBON_SERVICE_ABI_VERSION,
            .result = RIBON_TERMINAL_IMAGE_LAUNCH_RESULT_INVALID_SOURCE,
        };
    }
    if (opaque != (void *)1 || context == 0 ||
        request == 0 || receipt == 0 || request->size != sizeof(*request) ||
        request->abi_version != RIBON_SERVICE_ABI_VERSION ||
        request->image_data == 0 || request->image_size == 0u ||
        request->image_size > (uint64_t)(UINTN)-1 ||
        request->validated_image == 0 ||
        !ribon_validated_image_is_valid(request->validated_image) ||
        request->validated_image->format != RIBON_EXECUTABLE_FORMAT_PE_COFF ||
        request->validated_image->image_size != request->image_size ||
        !ribon_uefi_app_source_matches(context, request->source,
                                       request->source_name) ||
        !terminal_build_device_path(
            context, request->source_name, terminal_device_path)) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    if (!terminal_build_load_options(
            request, terminal_load_options, &load_options_size)) {
        receipt->result = RIBON_TERMINAL_IMAGE_LAUNCH_RESULT_OPTIONS_REJECTED;
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    watchdog_seconds = (UINTN)(request->watchdog_timeout_ms / 1000u +
        (request->watchdog_timeout_ms % 1000u != 0u ? 1u : 0u));
    if (watchdog_seconds == 0u || context->boot_services->LoadImage == 0 ||
        context->boot_services->StartImage == 0 ||
        context->boot_services->UnloadImage == 0 ||
        context->boot_services->SetWatchdogTimer == 0) {
        return RIBON_SERVICE_STATUS_BAD_ARGUMENT;
    }
    status = context->boot_services->LoadImage(
        FALSE, context->image_handle,
        (EFI_DEVICE_PATH_PROTOCOL *)terminal_device_path,
        (void *)request->image_data, (UINTN)request->image_size, &child);
    if (EFI_ERROR(status) || child == 0) {
        receipt->result = RIBON_TERMINAL_IMAGE_LAUNCH_RESULT_LOAD_FAILED;
        receipt->provider_status = status;
        return RIBON_SERVICE_STATUS_IO;
    }
    status = context->boot_services->HandleProtocol(
        child, &loaded_guid, (void **)&loaded);
    if (EFI_ERROR(status) || loaded == 0 ||
        loaded->DeviceHandle != context->device_handle) {
        receipt->result = RIBON_TERMINAL_IMAGE_LAUNCH_RESULT_INVALID_SOURCE;
        receipt->provider_status = status;
        (void)context->boot_services->UnloadImage(child);
        return RIBON_SERVICE_STATUS_IO;
    }
    loaded->LoadOptions = load_options_size == 0u ? 0 : terminal_load_options;
    loaded->LoadOptionsSize = load_options_size;
    status = context->boot_services->SetWatchdogTimer(
        watchdog_seconds, 0x10000u, 0u, 0);
    if (EFI_ERROR(status)) {
        receipt->result = RIBON_TERMINAL_IMAGE_LAUNCH_RESULT_START_RETURNED;
        receipt->provider_status = status;
        (void)context->boot_services->UnloadImage(child);
        return RIBON_SERVICE_STATUS_IO;
    }
    status = context->boot_services->StartImage(child, &exit_data_size, &exit_data);
    receipt->result = RIBON_TERMINAL_IMAGE_LAUNCH_RESULT_START_RETURNED;
    receipt->provider_status = status;
    receipt->exit_data_size = exit_data_size > 4096u ? 4096u : exit_data_size;
    (void)context->boot_services->SetWatchdogTimer(0u, 0u, 0u, 0);
    (void)context->boot_services->UnloadImage(child);
    return RIBON_SERVICE_STATUS_IO;
}

static struct RibonTerminalImageLaunchServiceOperations terminal_operations = {
    .size = sizeof(terminal_operations),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .context = (void *)1,
    .launch = terminal_launch,
};

const struct RibonServiceDescriptor
ribon_uefi_app_terminal_image_launch_service_descriptor = {
    .magic = RIBON_SERVICE_DESCRIPTOR_MAGIC,
    .size = sizeof(ribon_uefi_app_terminal_image_launch_service_descriptor),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .kind = RIBON_SERVICE_KIND_TERMINAL_IMAGE_LAUNCH,
    .cardinality = RIBON_SERVICE_CARDINALITY_AUTHORITY,
    .lifetime = RIBON_SERVICE_LIFETIME_BOOT,
    .phase = RIBON_PLUGIN_PHASE_BOOT,
    .id = "service.uefi-app.terminal-image-launch",
    .provides = RIBON_CAP_TERMINAL_IMAGE_LAUNCH,
    .architecture_mask = RIBON_ARCH_MASK_X86_64 | RIBON_ARCH_MASK_AARCH64,
    .environment_mask = RIBON_ENV_MASK_UEFI,
    .mode_mask = RIBON_MODE_MASK(RIBON_MODE_NORMAL) |
        RIBON_MODE_MASK(RIBON_MODE_DIAGNOSTIC),
    .arena_budget = 4096u,
    .input_budget = 64ull * 1024ull * 1024ull,
    .output_budget = 4096u,
    .deadline_ms = 30000u,
    .operations = &terminal_operations,
    .operations_size = sizeof(terminal_operations),
    .operations_abi = RIBON_SERVICE_ABI_VERSION,
    .validate_operations =
        ribon_terminal_image_launch_service_operations_are_valid,
};
