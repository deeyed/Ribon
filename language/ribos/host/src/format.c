#include "ribos/host/format.h"

#include <stdarg.h>
#include <stdio.h>

int
ribos_host_snprintf(
    char *output,
    size_t output_size,
    const char *format,
    ...)
{
    int status;
    va_list arguments;

    if (output == NULL || output_size == 0 || format == NULL) {
        return -1;
    }
    va_start(arguments, format);
    status = vsnprintf(output, output_size, format, arguments);
    va_end(arguments);
    return status;
}
