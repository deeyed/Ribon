#include <Ribon/core/context.h>
#include <Ribon/sdk/abi.h>

#include <stdio.h>

/** @brief 설치된 public header와 SDK ABI symbol만으로 embed 계약을 확인한다. */
int main(void) {
    const struct RibonSdkAbiDescriptor *abi = ribon_sdk_abi_descriptor();

    if (abi == 0 ||
        abi->magic != RIBON_SDK_ABI_MAGIC ||
        abi->size != sizeof(*abi) ||
        abi->sdk_abi_version != RIBON_SDK_ABI_VERSION ||
        abi->core_abi_version != RIBON_CORE_ABI_VERSION ||
        abi->plugin_abi_major != RIBON_PLUGIN_ABI_MAJOR ||
        abi->source_version_major != RIBON_VERSION_MAJOR ||
        abi->source_version_minor != RIBON_VERSION_MINOR ||
        abi->source_version_patch != RIBON_VERSION_PATCH) {
        fputs("library_embed: installed ABI tuple mismatch\n", stderr);
        return 1;
    }
    puts("RIBON-R5-OUT-OF-TREE-LIBRARY-EMBED-OK");
    return 0;
}
