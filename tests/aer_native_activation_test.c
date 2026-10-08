#include "../src/loader/research/aerNativeActivation.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t testTime;
uint64_t aerDriveboardRecorderMonotonicNs(void) { return ++testTime; }

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
        unsetenv("AER_NATIVE_ACTIVATION");
        aerNativeActivationInitialize("DVP-0015A", "HASH", "COMMIT");
        assert(!aerNativeActivationEnabled());
        aerNativeActivationShutdown();
        return 0;
    }
    if (!strcmp(argv[1], "wrong-revision")) {
        setenv("AER_NATIVE_ACTIVATION", "1", 1);
        aerNativeActivationInitialize("OTHER", "HASH", "COMMIT");
        assert(!aerNativeActivationEnabled());
        return 0;
    }

    setenv("AER_NATIVE_ACTIVATION", "1", 1);
    setenv("AER_NATIVE_ACTIVATION_OUTPUT", argv[2], 1);
    aerNativeActivationInitialize("DVP-0015A", "F16FC04D", "8b22490");
    assert(aerNativeActivationEnabled());
    aerNativeActivationInstallHooks(1);
    assert(aerNativeActivationSteeringReplacement());
    aerNativeActivationObserveInitReplacement(1);
    aerNativeActivationTestObserveState(11, 0);
    aerNativeActivationTestObserveState(12, 2);
    for (unsigned i = 1; i < 7; ++i) aerNativeActivationTestCountPipeline(i);
    aerNativeActivationTestObserveSevenByte(1);
    aerNativeActivationTestObserveSevenByte(0);
    aerNativeActivationShutdown();
    aerNativeActivationShutdown();

    char *text = readText(argv[2]);
    assert(strstr(text, "AER_NATIVE_ACTIVATION_V1"));
    assert(strstr(text, "\"init_driver_owner\":\"loader replacement\""));
    assert(strstr(text, "\"saw_driver_11\":true"));
    assert(strstr(text, "\"saw_driver_12\":true"));
    assert(strstr(text, "\"saw_check_2\":true"));
    assert(strstr(text, "\"name\":\"CabinetCtrl_Main\",\"count\":1"));
    assert(strstr(text, "\"from_CabinetCtrl_Off\":1"));
    assert(strstr(text, "\"other_or_unknown\":1"));
    assert(strstr(text, "\"capture_complete\":true"));
    free(text);
    return 0;
}
