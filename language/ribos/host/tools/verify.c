#include "ribos/vm/verifier.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *
ribos_verify_read_file(const char *path, size_t *length)
{
    FILE *input = fopen(path, "rb");
    long measured;
    uint8_t *bytes;

    if (input == NULL ||
        fseek(input, 0, SEEK_END) != 0 ||
        (measured = ftell(input)) < 0 ||
        fseek(input, 0, SEEK_SET) != 0) {
        if (input != NULL) {
            (void)fclose(input);
        }
        return NULL;
    }
    bytes = malloc((size_t)measured == 0 ? 1 : (size_t)measured);
    if (bytes == NULL ||
        fread(bytes, 1, (size_t)measured, input) !=
            (size_t)measured ||
        fclose(input) != 0) {
        free(bytes);
        return NULL;
    }
    *length = (size_t)measured;
    return bytes;
}

int
main(int argc, char **argv)
{
    uint8_t *artifact;
    void *workspace;
    void *workspace_storage;
    size_t artifact_size;
    size_t workspace_size;
    RibosVerifierReport report;
    RibosVerifierStatus status;
    int self_test_workspace = 0;
    const char *path;

    if (argc == 2) {
        path = argv[1];
    } else if (argc == 3 &&
        strcmp(argv[1], "--self-test-workspace") == 0) {
        self_test_workspace = 1;
        path = argv[2];
    } else {
        (void)fprintf(
            stderr,
            "usage: %s [--self-test-workspace] POLICY.rba\n",
            argv[0]);
        return 64;
    }
    artifact = ribos_verify_read_file(path, &artifact_size);
    if (artifact == NULL) {
        (void)fprintf(
            stderr,
            "RIBOS-VERIFIER-FAIL status=io-error file=%s\n",
            path);
        return 66;
    }
    status = ribos_verifier_workspace_size_v1(
        artifact,
        artifact_size,
        &workspace_size,
        &report);
    if (status != RIBOS_VERIFIER_OK) {
        free(artifact);
        (void)fprintf(
            stderr,
            "RIBOS-VERIFIER-FAIL status=%s subject=%u id=%u "
            "detail=%u file=%s\n",
            ribos_verifier_status_name(status),
            (unsigned)report.subject,
            report.subject_id,
            report.detail,
            path);
        return 2;
    }
    workspace_storage = malloc(
        workspace_size == SIZE_MAX ? 0 : workspace_size + 1);
    if (workspace_storage == NULL) {
        free(artifact);
        (void)fprintf(
            stderr,
            "RIBOS-VERIFIER-FAIL status=no-memory bytes=%zu file=%s\n",
            workspace_size,
            path);
        return 70;
    }
    workspace = (uint8_t *)workspace_storage + 1;
    if (self_test_workspace) {
        status = ribos_verify_artifact_stage2_v1(
            artifact,
            artifact_size,
            ribos_schema_reference_v1(),
            workspace,
            workspace_size - 1,
            &report);
        if (status != RIBOS_VERIFIER_WORKSPACE_TOO_SMALL) {
            free(workspace_storage);
            free(artifact);
            (void)fprintf(
                stderr,
                "RIBOS-VERIFIER-FAIL status=workspace-self-test "
                "actual=%s file=%s\n",
                ribos_verifier_status_name(status),
                path);
            return 2;
        }
    }
    status = ribos_verify_artifact_stage2_v1(
        artifact,
        artifact_size,
        ribos_schema_reference_v1(),
        workspace,
        workspace_size,
        &report);
    free(workspace_storage);
    free(artifact);
    if (status != RIBOS_VERIFIER_OK) {
        (void)fprintf(
            stderr,
            "RIBOS-VERIFIER-FAIL status=%s subject=%u id=%u "
            "detail=%u file=%s\n",
            ribos_verifier_status_name(status),
            (unsigned)report.subject,
            report.subject_id,
            report.detail,
            path);
        return 2;
    }
    (void)printf(
        "RIBOS-VERIFIER-STAGE2-OK types=%u functions=%u blocks=%u "
        "instructions=%u entry-frame=%u entry-stack=%llu "
        "call-depth=%u capabilities=0x%08x instruction-upper=%llu "
        "helper-upper=%llu workspace=%zu file=%s\n",
        report.verified_type_count,
        report.verified_function_count,
        report.verified_block_count,
        report.verified_instruction_count,
        report.recomputed_frame_bytes,
        (unsigned long long)report.recomputed_stack_bytes,
        report.recomputed_call_depth,
        report.recomputed_reachable_capabilities,
        (unsigned long long)
            report.recomputed_instruction_upper_bound,
        (unsigned long long)
            report.recomputed_helper_upper_bound,
        workspace_size,
        path);
    if (self_test_workspace) {
        (void)printf(
            "RIBOS-VERIFIER-WORKSPACE-BOUND-OK "
            "short-bytes=%zu unaligned=1\n",
            workspace_size - 1);
    }
    return 0;
}
