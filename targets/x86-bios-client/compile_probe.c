#include "../../src/environments/bios-client/bios_client.h"

#include <Ribon/platform/facts.h>

/**
 * @brief BIOS client product가 소비하는 E820, EDD, long-mode ABI를 compile-time에 고정한다.
 *
 * 이 probe는 reset vector, real-mode trampoline 또는 firmware provider를 구현하지 않는다.
 */
int ribon_bios_client_compile_probe(void) {
    struct RibonBiosLongModeContract contract = {
        .size = sizeof(contract),
        .a20_enabled = 1u,
        .long_mode_supported = 1u,
        .interrupts_masked = 1u,
        .page_table_base = 0x1000u,
        .entry_point = 0x200000u,
    };
    return ribon_bios_long_mode_contract_is_valid(&contract) &&
           ribon_platform_facts_are_valid(ribon_platform_selected()) ?
        0 :
        -1;
}
