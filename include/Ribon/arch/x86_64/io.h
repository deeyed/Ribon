#ifndef RIBON_ARCH_X86_64_IO_H
#define RIBON_ARCH_X86_64_IO_H

#include <stdint.h>

/** @brief x86_64 I/O port에서 byte를 읽는다. */
uint8_t ribon_x86_64_in8(uint16_t port);

/** @brief x86_64 I/O port에 byte를 기록한다. */
void ribon_x86_64_out8(uint16_t port, uint8_t value);

#endif
