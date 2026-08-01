#ifndef RIBON_ENVIRONMENTS_UEFI_APP_RECOVERY_NETWORK_H
#define RIBON_ENVIRONMENTS_UEFI_APP_RECOVERY_NETWORK_H

#include <Uefi.h>

#include <Ribon/network/recovery.h>

/** @brief UEFI bounded TFTP adapter의 discovery 결과다. */
enum RibonUefiRecoveryNetworkStatus {
    RIBON_UEFI_RECOVERY_NETWORK_OK = 0,
    RIBON_UEFI_RECOVERY_NETWORK_BAD_ARGUMENT = -1,
    RIBON_UEFI_RECOVERY_NETWORK_NOT_FOUND = -2,
    RIBON_UEFI_RECOVERY_NETWORK_AMBIGUOUS = -3,
    RIBON_UEFI_RECOVERY_NETWORK_FIRMWARE_ERROR = -4,
};

/** @brief 실제 firmware capability probe가 선택한 backend다. */
enum RibonUefiRecoveryNetworkBackend {
    RIBON_UEFI_RECOVERY_NETWORK_BACKEND_INVALID = 0,
    RIBON_UEFI_RECOVERY_NETWORK_BACKEND_PXE_BASE_CODE = 1,
    RIBON_UEFI_RECOVERY_NETWORK_BACKEND_SIMPLE_NETWORK = 2,
};

/**
 * @brief Product-selected 단일 PXE Base Code handle을 static IPv4 TFTP에 결합한다.
 *
 * Boot Services가 열린 recovery/provisioning phase에서만 호출한다. 함수는 caller allocation을
 * 요구하지 않으며, 성공 뒤 generated network service의 borrowed context가 유효해진다.
 */
int ribon_uefi_recovery_network_open(
    EFI_BOOT_SERVICES *boot_services,
    const struct RibonRecoveryNetworkProductBinding *binding);

/** @brief Open 후 선택된 exact firmware backend을 반환한다. */
enum RibonUefiRecoveryNetworkBackend ribon_uefi_recovery_network_backend(void);

/** @brief Product graph가 선택하는 UEFI bounded TFTP network service descriptor다. */
extern const struct RibonServiceDescriptor
    ribon_uefi_bounded_tftp_network_service_descriptor;

#endif
