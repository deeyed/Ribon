#include <Ribon/arch/ops.h>
#include <Ribon/boot/image.h>
#include <Ribon/plugin/descriptor.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void put_u16(unsigned char *bytes, uint32_t offset, uint16_t value) {
    bytes[offset] = (unsigned char)value;
    bytes[offset + 1u] = (unsigned char)(value >> 8u);
}

static void put_u32(unsigned char *bytes, uint32_t offset, uint32_t value) {
    for (uint32_t index = 0u; index < 4u; ++index) {
        bytes[offset + index] = (unsigned char)(value >> (index * 8u));
    }
}

static void put_u64(unsigned char *bytes, uint32_t offset, uint64_t value) {
    put_u32(bytes, offset, (uint32_t)value);
    put_u32(bytes, offset + 4u, (uint32_t)(value >> 32u));
}

static void make_pe32_plus(unsigned char image[1024]) {
    const uint32_t pe = 0x80u;
    const uint32_t optional = pe + 24u;
    const uint32_t section = optional + 112u;
    memset(image, 0, 1024u);
    image[0] = 'M';
    image[1] = 'Z';
    put_u32(image, 0x3cu, pe);
    memcpy(image + pe, "PE\0\0", 4u);
    put_u16(image, pe + 4u, 0x8664u);
    put_u16(image, pe + 6u, 1u);
    put_u16(image, pe + 20u, 112u);
    put_u16(image, optional, 0x20bu);
    put_u32(image, optional + 16u, 0x1000u);
    put_u64(image, optional + 24u, 0x00400000u);
    put_u32(image, optional + 32u, 0x1000u);
    memcpy(image + section, ".text", 5u);
    put_u32(image, section + 8u, 0x1000u);
    put_u32(image, section + 12u, 0x1000u);
    put_u32(image, section + 16u, 0x100u);
    put_u32(image, section + 20u, 0x200u);
    put_u32(image, section + 36u, 0x60000000u);
    image[0x200u] = 0xf4u;
}

int main(void) {
    unsigned char bytes[1024];
    struct RibonLoadSegment segments[2];
    struct RibonDirectLoadPlan layout = {
        .segments = segments,
        .segment_capacity = 2u,
    };
    struct RibonValidatedImage validated;
    const struct RibonArchDescriptor arch = {
        .size = sizeof(arch),
        .abi_version = RIBON_ARCH_OPS_ABI_VERSION,
        .id = RIBON_ARCHITECTURE_X86_64,
        .canonical_name = "x86_64",
        .word_bits = 64u,
        .physical_address_bits = 52u,
        .virtual_address_bits = 48u,
        .elf_machine = 62u,
        .pe_coff_machine = 0x8664u,
        .page_size = 4096u,
    };
    const struct RibonImageFormatOps *ops =
        (const struct RibonImageFormatOps *)
            ribon_pe_coff_image_plugin_descriptor.operations;
    struct RibonPayloadImage image;

    make_pe32_plus(bytes);
    image = (struct RibonPayloadImage){
        .data = bytes,
        .size = sizeof(bytes),
        .source_name = "fixture.pe",
    };
    if (!ribon_image_plugin_operations_are_valid(
            &ribon_pe_coff_image_plugin_descriptor) ||
        ops->analyze(&image, &validated, &layout) != RIBON_LOADER_STATUS_OK ||
        ribon_arch_validate_direct_load(&arch, &validated, &layout) !=
            RIBON_ARCH_OPERATION_OK ||
        validated.format != RIBON_EXECUTABLE_FORMAT_PE_COFF ||
        validated.machine != 0x8664u ||
        validated.image_size != sizeof(bytes) ||
        (validated.execution_support & RIBON_IMAGE_EXECUTION_FIRMWARE_MANAGED) == 0u ||
        layout.segment_count != 1u ||
        layout.entry_load_address != 0x00401000u ||
        layout.load_base != 0x00401000u ||
        layout.load_end != 0x00402000u ||
        segments[0].file_offset != 0x200u ||
        segments[0].file_size != 0x100u ||
        segments[0].memory_size != 0x1000u ||
        (segments[0].flags & RIBON_LOAD_SEGMENT_EXECUTE) == 0u) {
        fputs("pe_coff_loader_tests: valid fixture rejected\n", stderr);
        return 1;
    }

    bytes[0] = 0u;
    layout = (struct RibonDirectLoadPlan){
        .segments = segments,
        .segment_capacity = 2u,
    };
    if (ops->analyze(&image, &validated, &layout) != RIBON_LOADER_STATUS_BAD_FORMAT) {
        fputs("pe_coff_loader_tests: bad DOS header accepted\n", stderr);
        return 1;
    }
    make_pe32_plus(bytes);
    put_u32(bytes, 0x80u + 24u + 16u, 0x3000u);
    layout = (struct RibonDirectLoadPlan){
        .segments = segments,
        .segment_capacity = 2u,
    };
    if (ops->analyze(&image, &validated, &layout) !=
        RIBON_LOADER_STATUS_NO_LOAD_SEGMENTS) {
        fputs("pe_coff_loader_tests: uncovered entry accepted\n", stderr);
        return 1;
    }
    make_pe32_plus(bytes);
    put_u64(bytes, 0x80u + 24u + 24u, 0u);
    if (ops->analyze(&image, &validated, 0) != RIBON_LOADER_STATUS_OK ||
        validated.format != RIBON_EXECUTABLE_FORMAT_PE_COFF) {
        fputs("pe_coff_loader_tests: relocatable managed image rejected\n", stderr);
        return 1;
    }
    layout = (struct RibonDirectLoadPlan){
        .segments = segments,
        .segment_capacity = 2u,
    };
    if (ops->analyze(&image, &validated, &layout) !=
        RIBON_LOADER_STATUS_BAD_FORMAT) {
        fputs("pe_coff_loader_tests: zero-base direct image accepted\n", stderr);
        return 1;
    }
    puts("RIBON-PE-COFF-LOADER-TESTS-OK");
    return 0;
}
