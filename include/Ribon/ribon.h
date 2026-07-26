#ifndef RIBON_RIBON_H
#define RIBON_RIBON_H

#include <stdint.h>

#include <Ribon/arch.h>
#include <Ribon/firmware.h>
#include <Ribon/loader.h>
#include <Ribon/profile.h>

#define RIBON_VERSION_MAJOR 0u
#define RIBON_VERSION_MINOR 1u
#define RIBON_VERSION_PATCH 0u

enum RibonStatus {
    RIBON_STATUS_OK = 0,
    RIBON_STATUS_BAD_ARGUMENT = -1,
    RIBON_STATUS_UNSUPPORTED_PROFILE = -2,
    RIBON_STATUS_UNSUPPORTED_ARCH = -3,
    RIBON_STATUS_INCOMPLETE_ENVIRONMENT = -4,
    RIBON_STATUS_INVALID_PAYLOAD = -5,
    RIBON_STATUS_INVALID_HANDOFF = -6,
};

struct RibonBootRequest {
    const struct RibonBootEnvironment *environment;
    const struct RibonProfile *profile;
    struct RibonMutableMemoryMap *normalized_memory_map;
    const struct RibonPayloadImage *kernel_payload;
    struct RibonLoadedPayload *kernel_layout;
    void *handoff_buffer;
    uint64_t handoff_buffer_capacity;
    struct RibonHandoffArtifact *handoff_artifact;
};

struct RibonBootPlan {
    enum RibonFirmwareKind firmware;
    const struct RibonArchDescriptor *arch;
    uint32_t environment_flags;
    uint32_t memory_region_count;
    uint32_t normalized_memory_region_count;
    uint64_t usable_memory_bytes;
    enum RibonBootMediaKind boot_media;
    uint32_t boot_module_count;
    uint64_t device_tree_address;
    uint64_t device_tree_size;
    uint64_t framebuffer_address;
    const char *command_line;
    const char *profile_name;
    const char *kernel_path;
    const char *kernel_source_name;
    const char *handoff_name;
    const char *handoff_artifact_format;
    enum RibonExecutableFormat kernel_format;
    uint16_t kernel_machine;
    uint32_t kernel_load_segment_count;
    uint32_t handoff_artifact_sections;
    uint64_t kernel_entry_point;
    uint64_t kernel_entry_load_address;
    uint64_t kernel_runtime_entry_address;
    uint64_t kernel_load_base;
    uint64_t kernel_load_end;
    uint64_t kernel_runtime_load_base;
    uint64_t kernel_runtime_load_end;
    uint64_t kernel_memory_size;
    uint64_t kernel_linked_virtual_base;
    uint64_t kernel_linked_virtual_end;
    uint64_t kernel_linked_physical_base;
    uint64_t kernel_linked_physical_end;
    uint64_t kernel_high_entry_virtual_address;
    uint64_t kernel_high_entry_load_address;
    const struct RibonLoadSegment *kernel_load_segments;
    uint64_t handoff_artifact_size;
    uint32_t kernel_load_plan_flags;
    uint32_t expectations;
    enum RibonHandoffKind handoff;
    uint32_t handoff_major;
};

const char *ribon_version_string(void);
int ribon_build_plan(const struct RibonBootRequest *request, struct RibonBootPlan *out);

#endif
