#ifndef RIBON_BOOT_TERMINAL_H
#define RIBON_BOOT_TERMINAL_H

/** @brief Firmware-managed image에 전달할 OS-neutral option encoding이다. */
enum RibonTerminalLoadOptionsKind {
    RIBON_TERMINAL_LOAD_OPTIONS_NONE = 0,
    RIBON_TERMINAL_LOAD_OPTIONS_UTF8_COMMAND_LINE = 1,
};

#endif
