#ifndef RIBON_PLUGIN_PHASES_H
#define RIBON_PLUGIN_PHASES_H

/** @brief Plugin dependency가 따르는 단방향 lifecycle phase다. */
enum RibonPluginPhase {
    RIBON_PLUGIN_PHASE_EARLY = 0,
    RIBON_PLUGIN_PHASE_FOUNDATION = 1,
    RIBON_PLUGIN_PHASE_DRIVER = 2,
    RIBON_PLUGIN_PHASE_BOOT = 3,
    RIBON_PLUGIN_PHASE_QUIESCE = 4,
    RIBON_PLUGIN_PHASE_RUNTIME = 5,
};

/** @brief Plugin phase의 안정적인 이름을 반환한다. */
const char *ribon_plugin_phase_name(enum RibonPluginPhase phase);

#endif
