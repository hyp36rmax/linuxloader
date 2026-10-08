#include "../src/loader/research/aerActivationDiagnostics.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MH_OK 0
#define MH_ERROR_NOT_EXECUTABLE 7
#define MH_ERROR_MEMORY_PROTECT 10

static int failPatch;

int patchMemoryFromStringResult(size_t address, const char *value)
{
    if (failPatch)
        return -2;
    size_t length = strlen(value) / 2;
    for (size_t i = 0; i < length; ++i) {
        char pair[3] = {value[i * 2], value[i * 2 + 1], 0};
        ((unsigned char *)address)[i] = (unsigned char)strtoul(pair, NULL, 16);
    }
    return 0;
}

int MH_CreateHook(void *target, void *replacement, void **original)
{
    (void)target; (void)replacement; (void)original;
    return MH_OK;
}

static char *readText(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    char *text = calloc((size_t)size + 1, 1);
    assert(text);
    assert(fread(text, (size_t)size, 1, file) == 1);
    fclose(file);
    return text;
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    if (!strcmp(argv[1], "disabled")) {
        unsetenv("AER_ACTIVATION_DIAGNOSTICS");
        aerActivationDiagnosticsInitialize("DVP-0015A");
        assert(!aerActivationDiagnosticsEnabled());
        aerActivationDiagnosticsShutdown();
        return 0;
    }

    setenv("AER_ACTIVATION_DIAGNOSTICS", "1", 1);
    setenv("AER_ACTIVATION_DIAGNOSTICS_OUTPUT", argv[2], 1);
    aerActivationDiagnosticsInitialize("DVP-0015A");
    aerActivationDiagnosticsInitialize("DVP-0015A");
    assert(aerActivationDiagnosticsEnabled());
#if defined(__linux__)
    assert(aerActivationDiagnosticsUsesAtexit() == 0);
#else
    assert(aerActivationDiagnosticsUsesAtexit() == 1);
#endif

    unsigned char success[2] = {0x12, 0x34};
    aerActivationDiagnosticsPatch((size_t)success, "AABB", "success");
    assert(success[0] == 0xAA && success[1] == 0xBB);
    unsigned char mismatch[1] = {0x44};
    failPatch = 1;
    aerActivationDiagnosticsPatch((size_t)mismatch, "55", "mismatch");
    assert(mismatch[0] == 0x44);

    aerActivationDiagnosticsTestHookResult(0x08103eaa, "hook-success", MH_OK);
    aerActivationDiagnosticsTestHookResult(0x08105d88, "hook-failure", MH_ERROR_NOT_EXECUTABLE);
    aerActivationDiagnosticsHooksEnabled(MH_ERROR_MEMORY_PROTECT);
    aerActivationDiagnosticsSelectReadable();
    aerActivationDiagnosticsSelectReadable();
    aerActivationDiagnosticsIoctl(1, 0x11);
    aerActivationDiagnosticsIoctl(0, 0x11);
    aerActivationDiagnosticsRead(0, 0, 0x11);
    aerActivationDiagnosticsRead(1, 1, 0x00);
    aerActivationDiagnosticsResponseTransition(0x11, 0x00);
    aerActivationDiagnosticsFirstWrite(1234, 1, 5, 7, 0, 0, 0x11);
    aerActivationDiagnosticsFirstWriteResult(7);
    aerActivationDiagnosticsFirstWrite(9999, 2, 6, 4, 1, 1, 0x00);
    aerActivationDiagnosticsShutdown();
    aerActivationDiagnosticsShutdown();

    char *text = readText(argv[2]);
    assert(strstr(text, "AER_DRIVEBOARD_ACTIVATION_V1"));
    assert(strstr(text, "\"verified\":true"));
    assert(strstr(text, "\"verified\":false"));
    assert(strstr(text, "\"creation_result\":7"));
    assert(strstr(text, "\"enable_result\":10"));
    assert(strstr(text, "\"select_readable\":2"));
    assert(strstr(text, "\"reads_enable_false\":1"));
    assert(strstr(text, "\"reads_enable_true\":1"));
    assert(strstr(text, "\"reads_without_valid_response\":1"));
    assert(strstr(text, "\"timestamp_ns\":1234"));
    assert(strstr(text, "\"requested_length\":7"));
    assert(strstr(text, "\"actual_result\":7"));
    assert(strstr(text, "\"capture_complete\": true"));
    free(text);
    return 0;
}
