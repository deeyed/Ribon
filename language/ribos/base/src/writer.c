#include "ribos/base/writer.h"

#include <stdarg.h>

int
ribos_writer_printf(
    RibosWriter *writer,
    const char *format,
    ...)
{
    int status;
    va_list arguments;

    if (writer == NULL || writer->format == NULL || format == NULL) {
        return -1;
    }
    va_start(arguments, format);
    status = writer->format(writer->context, format, arguments);
    va_end(arguments);
    return status;
}
