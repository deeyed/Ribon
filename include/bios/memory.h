#ifndef RIBON_BIOS_MEMORY_H
#define RIBON_BIOS_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#define RIBON_BIOS_E820_TYPE_USABLE 1u
#define RIBON_BIOS_E820_TYPE_RESERVED 2u
#define RIBON_BIOS_E820_TYPE_ACPI_RECLAIM 3u
#define RIBON_BIOS_E820_TYPE_ACPI_NVS 4u
#define RIBON_BIOS_E820_TYPE_BAD_MEMORY 5u

struct RibonBiosE820Entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t attributes;
};

struct RibonBiosE820Table {
    struct RibonBiosE820Entry *entries;
    size_t capacity;
    size_t count;
};

int ribon_bios_collect_e820(struct RibonBiosE820Table *table);

#endif
