#ifndef RIBON_VALIDATION_UEFI_UPDATE_PROTECTED_STATE_H
#define RIBON_VALIDATION_UEFI_UPDATE_PROTECTED_STATE_H

#include <Ribon/security/protected_state.h>
#include <Ribon/update/storage.h>

/** @brief QEMU reference provider를 selected update-media tail에 결속한다. */
int ribon_qemu_update_protected_state_bind(
    const struct RibonUpdateStorageProvider *storage,
    const struct RibonUpdateLayout *layout,
    const uint8_t domain[RIBON_PROTECTED_STATE_DIGEST_BYTES]);

#endif
