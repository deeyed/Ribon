#ifndef RIBON_ENVIRONMENTS_UEFI_TERMINAL_IMAGE_H
#define RIBON_ENVIRONMENTS_UEFI_TERMINAL_IMAGE_H

#include <Ribon/service/directory.h>

/** @brief Optional UEFI firmware-managed image launch authority다. */
extern const struct RibonServiceDescriptor
    ribon_uefi_app_terminal_image_launch_service_descriptor;

#endif
