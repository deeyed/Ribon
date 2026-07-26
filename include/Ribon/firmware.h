#ifndef RIBON_FIRMWARE_H
#define RIBON_FIRMWARE_H

#include <stdint.h>

#include <Ribon/memory.h>

struct RibonArchDescriptor;

enum RibonFirmwareKind {
    RIBON_FIRMWARE_HOST = 0,
    RIBON_FIRMWARE_UEFI = 1,
    RIBON_FIRMWARE_BIOS = 2,
    RIBON_FIRMWARE_RASPBERRY_PI = 3,
};

enum RibonFirmwareStatus {
    RIBON_FIRMWARE_STATUS_OK = 0,
    RIBON_FIRMWARE_STATUS_BAD_ARGUMENT = -1,
    RIBON_FIRMWARE_STATUS_UNSUPPORTED = -2,
};

enum RibonBootMediaKind {
    RIBON_BOOT_MEDIA_NONE = 0,
    RIBON_BOOT_MEDIA_FILE = 1,
    RIBON_BOOT_MEDIA_BLOCK = 2,
    RIBON_BOOT_MEDIA_MEMORY = 3,
    RIBON_BOOT_MEDIA_NETWORK = 4,
};

enum RibonBootEnvironmentFlags {
    RIBON_BOOT_ENV_HAS_MEMORY_MAP = 1u << 0,
    RIBON_BOOT_ENV_HAS_DEVICE_TREE = 1u << 1,
    RIBON_BOOT_ENV_HAS_FRAMEBUFFER = 1u << 2,
    RIBON_BOOT_ENV_HAS_BOOT_MEDIA = 1u << 3,
    RIBON_BOOT_ENV_HAS_COMMAND_LINE = 1u << 4,
    RIBON_BOOT_ENV_HAS_BOOT_MODULES = 1u << 5,
    RIBON_BOOT_ENV_HAS_RAW_MEMORY_MAP = 1u << 6,
    RIBON_BOOT_ENV_HAS_ACPI = 1u << 7,
};

struct RibonDeviceTree {
    uint64_t physical_address;
    uint64_t size;
    const void *data;
};

enum RibonFramebufferBackend {
    RIBON_FRAMEBUFFER_BACKEND_UNKNOWN = 0,
    RIBON_FRAMEBUFFER_BACKEND_UEFI_GOP = 1,
    RIBON_FRAMEBUFFER_BACKEND_VBE = 2,
    RIBON_FRAMEBUFFER_BACKEND_VGA_TEXT = 3,
};

struct RibonFramebufferRgbInfo {
    uint8_t red_position;
    uint8_t red_mask_size;
    uint8_t green_position;
    uint8_t green_mask_size;
    uint8_t blue_position;
    uint8_t blue_mask_size;
};

struct RibonFramebuffer {
    uint64_t physical_address;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bits_per_pixel;
    enum RibonFramebufferBackend backend;
    struct RibonFramebufferRgbInfo rgb;
};

struct RibonRawMemoryMap {
    const void *data;
    uint64_t size;
    uint32_t descriptor_size;
    uint32_t descriptor_version;
};

struct RibonAcpiRsdp {
    uint64_t physical_address;
    const void *data;
    uint32_t size;
    uint32_t revision;
};

struct RibonBootMedia {
    enum RibonBootMediaKind kind;
    const char *path;
    uint64_t physical_address;
    uint64_t size;
    uint32_t block_size;
};

struct RibonCommandLine {
    const char *text;
    uint32_t length;
};

struct RibonBootModule {
    const char *name;
    uint64_t physical_address;
    uint64_t size;
    uint32_t flags;
};

struct RibonBootModuleList {
    const struct RibonBootModule *modules;
    uint32_t module_count;
};

struct RibonBootEnvironment {
    enum RibonFirmwareKind firmware;
    const struct RibonArchDescriptor *arch;
    struct RibonMemoryMap memory_map;
    struct RibonRawMemoryMap raw_memory_map;
    struct RibonDeviceTree device_tree;
    struct RibonFramebuffer framebuffer;
    struct RibonAcpiRsdp acpi_rsdp;
    struct RibonBootMedia boot_media;
    struct RibonBootModuleList boot_modules;
    struct RibonCommandLine command_line;
    uint32_t flags;
};

struct RibonFirmwareAdapter {
    enum RibonFirmwareKind firmware;
    const char *name;
    int (*probe)(const struct RibonArchDescriptor *arch);
    int (*collect)(const struct RibonArchDescriptor *arch, struct RibonBootEnvironment *out);
};

const char *ribon_firmware_name(enum RibonFirmwareKind firmware);
const char *ribon_boot_media_name(enum RibonBootMediaKind media);
uint32_t ribon_firmware_arch_mask(enum RibonFirmwareKind firmware);
void ribon_boot_environment_init(
    struct RibonBootEnvironment *environment,
    enum RibonFirmwareKind firmware,
    const struct RibonArchDescriptor *arch);
int ribon_boot_environment_is_valid(const struct RibonBootEnvironment *environment);
int ribon_firmware_adapter_supports_arch(
    const struct RibonFirmwareAdapter *adapter,
    const struct RibonArchDescriptor *arch);

const struct RibonFirmwareAdapter *ribon_firmware_host_adapter(void);
const struct RibonFirmwareAdapter *ribon_firmware_uefi_adapter(void);
const struct RibonFirmwareAdapter *ribon_firmware_bios_adapter(void);
const struct RibonFirmwareAdapter *ribon_firmware_rpi_adapter(void);

#endif
