#include <Ribon/arch/x86_64/io.h>

/** @brief x86_64 I/O port에서 byte를 읽는다. */
uint8_t ribon_x86_64_in8(uint16_t port) {
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/** @brief x86_64 I/O port에 byte를 기록한다. */
void ribon_x86_64_out8(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}
