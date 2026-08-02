#include <Ribon/sdk/abi.h>
#include <Ribon/core/capability.h>
#include <Ribon/update/manifest.h>

#include <stdio.h>

/** @brief Installed public ABI와 independent update reader만 소비한다. */
int main(int argc, char **argv) {
    unsigned char bytes[4096];
    struct RibonUpdateManifestView view;
    const struct RibonSdkAbiDescriptor *abi;
    FILE *input;
    size_t size;

    if (argc != 2 || (input = fopen(argv[1], "rb")) == 0) {
        return 2;
    }
    size = fread(bytes, 1u, sizeof(bytes), input);
    if (fclose(input) != 0 || size == 0u || size == sizeof(bytes)) {
        return 3;
    }
    abi = ribon_sdk_abi_descriptor();
    if (abi == 0 || abi->sdk_abi_version != RIBON_SDK_ABI_VERSION ||
        abi->core_abi_version != RIBON_CORE_ABI_VERSION ||
        ribon_update_manifest_open(bytes, size, &view) !=
            RIBON_UPDATE_MANIFEST_STATUS_OK ||
        view.component_count != 1u ||
        view.mode != RIBON_KEY_POLICY_MODE_RECOVERY) {
        return 4;
    }
    puts("RIBON-D08-EXTERNAL-DEPLOYMENT-CONSUMER-OK");
    return 0;
}
