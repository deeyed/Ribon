#include "ribos/frontend/parser.h"
#include "ribos/ir/analysis.h"
#include "ribos/ir/ir.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct CountingHeader {
    size_t size;
    max_align_t alignment;
} CountingHeader;

typedef struct CountingAllocator {
    size_t live_allocations;
    size_t live_bytes;
    size_t size_mismatches;
} CountingAllocator;

static void *
counting_allocate(void *context, size_t size, size_t alignment)
{
    CountingAllocator *counting = context;
    CountingHeader *header;

    if (alignment > _Alignof(max_align_t) ||
        size > SIZE_MAX - sizeof(*header)) {
        return NULL;
    }
    header = malloc(sizeof(*header) + size);
    if (header == NULL) {
        return NULL;
    }
    header->size = size;
    ++counting->live_allocations;
    counting->live_bytes += size;
    return header + 1;
}

static void *
counting_resize(
    void *context,
    void *storage,
    size_t old_size,
    size_t new_size,
    size_t alignment)
{
    CountingAllocator *counting = context;
    CountingHeader *header = (CountingHeader *)storage - 1;
    CountingHeader *replacement;

    if (alignment > _Alignof(max_align_t) ||
        new_size > SIZE_MAX - sizeof(*replacement)) {
        return NULL;
    }
    if (header->size != old_size) {
        ++counting->size_mismatches;
    }
    replacement = realloc(header, sizeof(*replacement) + new_size);
    if (replacement == NULL) {
        return NULL;
    }
    counting->live_bytes -= replacement->size;
    replacement->size = new_size;
    counting->live_bytes += new_size;
    return replacement + 1;
}

static void
counting_deallocate(
    void *context,
    void *storage,
    size_t size,
    size_t alignment)
{
    CountingAllocator *counting = context;
    CountingHeader *header = (CountingHeader *)storage - 1;

    if (alignment > _Alignof(max_align_t) || header->size != size) {
        ++counting->size_mismatches;
    }
    --counting->live_allocations;
    counting->live_bytes -= header->size;
    free(header);
}

int
main(void)
{
    static const char source[] =
        "@policy\n"
        "def boot(ctx: BootContext) -> BootResult {\n"
        "    return boot.recovery(RecoveryReason.REQUESTED)\n"
        "}\n";
    CountingAllocator counting = {0};
    RibosAllocator allocator = {
        .context = &counting,
        .allocate = counting_allocate,
        .resize = counting_resize,
        .deallocate = counting_deallocate,
    };
    RibosParseSummary summary;
    RibosDiagnostic diagnostic;
    RibosIrModule *module;
    RibosIrResourceClosure *closure;
    int passed =
        ribos_parse_source(
            &allocator,
            source,
            strlen(source),
            &summary,
            &diagnostic) == RIBOS_PARSE_OK &&
        summary.declaration_count == 1;

    module = ribos_ir_module_create(&allocator);
    closure = ribos_ir_resource_closure_create(&allocator);
    passed = passed && module != NULL && closure != NULL;
    ribos_ir_resource_closure_destroy(closure);
    ribos_ir_module_destroy(module);
    passed = passed &&
        counting.live_allocations == 0 &&
        counting.live_bytes == 0 &&
        counting.size_mismatches == 0;
    if (!passed) {
        (void)fprintf(
            stderr,
            "RIBOS-ALLOCATOR-BOUNDARY-FAIL live=%zu bytes=%zu mismatch=%zu\n",
            counting.live_allocations,
            counting.live_bytes,
            counting.size_mismatches);
        return 1;
    }
    (void)printf(
        "RIBOS-ALLOCATOR-BOUNDARY-OK live=0 bytes=0 size-mismatch=0\n");
    return 0;
}
