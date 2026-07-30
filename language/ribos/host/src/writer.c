#include "ribos/host/writer.h"

#include <stdarg.h>
#include <stdio.h>

static int
ribos_host_file_format(
    void *context,
    const char *format,
    va_list arguments)
{
    FILE *stream = context;

    if (stream == NULL) {
        return -1;
    }
    return vfprintf(stream, format, arguments);
}

RibosWriter
ribos_host_file_writer(FILE *stream)
{
    return (RibosWriter){
        .context = stream,
        .format = ribos_host_file_format,
    };
}
