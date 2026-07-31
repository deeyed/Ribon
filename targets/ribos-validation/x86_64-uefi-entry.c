#include <stdint.h>

#include <Uefi.h>

_Noreturn void ribon_raw_fdt_boot_main(uint64_t native0, uint64_t native1);

/** @brief UEFI loader가 시작하는 diagnostic-only Ribos validation entry다. */
EFI_STATUS EFIAPI
efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table)
{
    ribon_raw_fdt_boot_main(
        (uint64_t)(uintptr_t)image_handle,
        (uint64_t)(uintptr_t)system_table);
}
