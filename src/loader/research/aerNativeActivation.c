#include "aerNativeActivation.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aerDriveboardRecorder.h"
#ifndef AER_NATIVE_ACTIVATION_TESTING
#include "../patching/flowControl.h"
#endif

#define DRIVER_STATE_ADDRESS ((volatile int *)0x0866D460)
#define CHECK_STATE_ADDRESS ((volatile int *)0x0866D48C)
#define PIPELINE_COUNT 7

enum PipelineIndex {
    PIPE_INIT_DRIVER,
    PIPE_CABINET_CHECK,
    PIPE_CABINET_MAIN,
    PIPE_DATA_SET,
    PIPE_MOVE_SEND,
    PIPE_STEER_SEND_OUT,
    PIPE_HARDCOM_SEND
};

typedef struct {
    int initialized;
    int enabled;
    int steeringReplacement;
    int initialDriverState;
    int initialCheckState;
    int finalDriverState;
    int finalCheckState;
    int sawDriver11;
    int sawDriver12;
    int sawCheck2;
    uint64_t firstTimestamp[PIPELINE_COUNT];
    uint64_t lastTimestamp[PIPELINE_COUNT];
    atomic_ullong counts[PIPELINE_COUNT];
    atomic_ullong sevenByteWrites;
    atomic_ullong sevenByteFromCabinetOff;
    atomic_ullong sevenByteUnknownCaller;
    char revision[32];
    char executableHash[80];
    char loaderCommit[64];
    char outputPath[1024];
    int hooksAttempted;
    int hooksComplete;
} NativeActivationState;

static NativeActivationState g_native;
static _Thread_local int g_insideCabinetOff;
static _Thread_local int g_zeroSevenCandidate;

#ifndef AER_NATIVE_ACTIVATION_TESTING
typedef int (*InitDriverFn)(void);
typedef void (*OnePointerFn)(void *);
typedef void (*DataSetFn)(void *, const void *, const void *, const void *);
typedef void (*MoveSendFn)(void *, const void *, const void *);
typedef int (*SendOutFn)(unsigned char *);
typedef int (*HardcomSendFn)(int, unsigned char *, int);
typedef void (*NoArgsFn)(void);

static InitDriverFn g_originalInitDriver;
static OnePointerFn g_originalCabinetCheck;
static OnePointerFn g_originalCabinetMain;
static DataSetFn g_originalDataSet;
static MoveSendFn g_originalMoveSend;
static SendOutFn g_originalSendOut;
static HardcomSendFn g_originalHardcomSend;
static NoArgsFn g_originalCabinetOff;
#endif

static int envEnabled(void)
{
    const char *value = getenv("AER_NATIVE_ACTIVATION");
    return value && (!strcmp(value, "1") || !strcmp(value, "true") || !strcmp(value, "TRUE"));
}

static void observeStateValues(int driverState, int checkState)
{
    g_native.finalDriverState = driverState;
    g_native.finalCheckState = checkState;
    if (driverState == 11) g_native.sawDriver11 = 1;
    if (driverState == 12) g_native.sawDriver12 = 1;
    if (checkState == 2) g_native.sawCheck2 = 1;
}

static void observeState(void)
{
    if (!g_native.enabled) return;
#ifdef AER_NATIVE_ACTIVATION_TESTING
    observeStateValues(g_native.finalDriverState, g_native.finalCheckState);
#else
    observeStateValues(*DRIVER_STATE_ADDRESS, *CHECK_STATE_ADDRESS);
#endif
}

static void countPipeline(unsigned index)
{
    if (!g_native.enabled || index >= PIPELINE_COUNT) return;
    uint64_t now = aerDriveboardRecorderMonotonicNs();
    if (atomic_fetch_add(&g_native.counts[index], 1) == 0)
        g_native.firstTimestamp[index] = now;
    g_native.lastTimestamp[index] = now;
    observeState();
}

void aerNativeActivationInitialize(const char *revision, const char *hash, const char *commit)
{
    if (g_native.initialized) return;
    memset(&g_native, 0, sizeof(g_native));
    g_native.initialized = 1;
    if (!envEnabled() || !revision || strcmp(revision, "DVP-0015A")) return;
    g_native.enabled = 1;
    snprintf(g_native.revision, sizeof(g_native.revision), "%s", revision);
    snprintf(g_native.executableHash, sizeof(g_native.executableHash), "%s", hash ? hash : "UNKNOWN");
    snprintf(g_native.loaderCommit, sizeof(g_native.loaderCommit), "%s", commit ? commit : "UNKNOWN");
    const char *path = getenv("AER_NATIVE_ACTIVATION_OUTPUT");
    snprintf(g_native.outputPath, sizeof(g_native.outputPath), "%s",
             path && path[0] ? path : "aer_native_activation.json");
    for (unsigned i = 0; i < PIPELINE_COUNT; ++i) atomic_init(&g_native.counts[i], 0);
    atomic_init(&g_native.sevenByteWrites, 0);
    atomic_init(&g_native.sevenByteFromCabinetOff, 0);
    atomic_init(&g_native.sevenByteUnknownCaller, 0);
#ifndef AER_NATIVE_ACTIVATION_TESTING
    g_native.initialDriverState = *DRIVER_STATE_ADDRESS;
    g_native.initialCheckState = *CHECK_STATE_ADDRESS;
#endif
    g_native.finalDriverState = g_native.initialDriverState;
    g_native.finalCheckState = g_native.initialCheckState;
#if !defined(__linux__)
    atexit(aerNativeActivationShutdown);
#endif
}

int aerNativeActivationEnabled(void) { return g_native.enabled; }
int aerNativeActivationSteeringReplacement(void) { return g_native.steeringReplacement; }

void aerNativeActivationObserveInitReplacement(int returnValue)
{
    (void)returnValue;
    countPipeline(PIPE_INIT_DRIVER);
}

#ifndef AER_NATIVE_ACTIVATION_TESTING
static int observeInitDriver(void)
{
    countPipeline(PIPE_INIT_DRIVER);
    int result = g_originalInitDriver();
    observeState();
    return result;
}

static void observeCabinetCheck(void *work)
{
    countPipeline(PIPE_CABINET_CHECK);
    g_originalCabinetCheck(work);
    observeState();
}

static void observeCabinetMain(void *work)
{
    countPipeline(PIPE_CABINET_MAIN);
    g_originalCabinetMain(work);
}

static void observeDataSet(void *work, const void *car, const void *carWork, const void *camera)
{
    countPipeline(PIPE_DATA_SET);
    g_originalDataSet(work, car, carWork, camera);
}

static void observeMoveSend(void *work, const void *car, const void *carWork)
{
    countPipeline(PIPE_MOVE_SEND);
    g_originalMoveSend(work, car, carWork);
}

static int observeSendOut(unsigned char *bytes)
{
    countPipeline(PIPE_STEER_SEND_OUT);
    g_zeroSevenCandidate = bytes && !bytes[0] && !bytes[1] && !bytes[2] &&
                           !bytes[3] && !bytes[4] && !bytes[5];
    int result = g_originalSendOut(bytes);
    g_zeroSevenCandidate = 0;
    return result;
}

static int observeHardcomSend(int channel, unsigned char *bytes, int length)
{
    countPipeline(PIPE_HARDCOM_SEND);
    if (channel == 0 && length == 7 && bytes && bytes[0] == 0x80 &&
        !bytes[1] && !bytes[2] && !bytes[3] && !bytes[4] && !bytes[5] && !bytes[6]) {
        atomic_fetch_add(&g_native.sevenByteWrites, 1);
        if (g_insideCabinetOff && g_zeroSevenCandidate)
            atomic_fetch_add(&g_native.sevenByteFromCabinetOff, 1);
        else
            atomic_fetch_add(&g_native.sevenByteUnknownCaller, 1);
    }
    return g_originalHardcomSend(channel, bytes, length);
}

static void observeCabinetOff(void)
{
    g_insideCabinetOff++;
    g_originalCabinetOff();
    g_insideCabinetOff--;
}

static int installHook(size_t address, void *replacement, void **original)
{
    *original = trampolineHook((void *)address, replacement, 16);
    return *original != NULL;
}
#endif

void aerNativeActivationInstallHooks(int steeringReplacementActive)
{
    if (!g_native.enabled) return;
    g_native.steeringReplacement = steeringReplacementActive != 0;
    g_native.hooksAttempted = 1;
#ifdef AER_NATIVE_ACTIVATION_TESTING
    g_native.hooksComplete = 1;
#elif defined(_WIN32)
    int ok = 1;
    if (!g_native.steeringReplacement)
        ok &= installHook(0x08103EAA, (void *)observeInitDriver, (void **)&g_originalInitDriver);
    ok &= installHook(0x0810477E, (void *)observeCabinetCheck, (void **)&g_originalCabinetCheck);
    ok &= installHook(0x081048B2, (void *)observeCabinetMain, (void **)&g_originalCabinetMain);
    ok &= installHook(0x08104F02, (void *)observeDataSet, (void **)&g_originalDataSet);
    ok &= installHook(0x081051F4, (void *)observeMoveSend, (void **)&g_originalMoveSend);
    ok &= installHook(0x08105AD2, (void *)observeSendOut, (void **)&g_originalSendOut);
    ok &= installHook(0x0810735E, (void *)observeHardcomSend, (void **)&g_originalHardcomSend);
    ok &= installHook(0x081055FE, (void *)observeCabinetOff, (void **)&g_originalCabinetOff);
    g_native.hooksComplete = ok;
#else
    /* The legacy Linux trampoline copier cannot prove instruction-boundary or
       PC-relative safety. Keep Linux builds observationally non-invasive. */
    g_native.hooksComplete = 0;
#endif
}

void aerNativeActivationShutdown(void)
{
    if (!g_native.enabled) return;
    observeState();
    FILE *file = fopen(g_native.outputPath, "wb");
    if (!file) return;
    static const char *names[PIPELINE_COUNT] = {
        "CabinetCtrl_InitDriver", "CabinetCtrl_Check", "CabinetCtrl_Main",
        "DrCtrlDataSet", "DrCtrlMoveSend", "steerReqSendOut", "hardcomSend"
    };
    fprintf(file, "{\n  \"schema\":\"%s\",\n", AER_NATIVE_ACTIVATION_VERSION);
    fprintf(file, "  \"game_revision\":\"%s\",\n  \"executable_sha256\":\"%s\",\n  \"loader_commit\":\"%s\",\n",
            g_native.revision, g_native.executableHash, g_native.loaderCommit);
    fprintf(file, "  \"observation_method\":\"preserving function trampolines and read-only native state reads\",\n");
    fprintf(file, "  \"hooks\":{\"attempted\":%s,\"complete\":%s,\"init_driver_owner\":\"%s\"},\n",
            g_native.hooksAttempted ? "true" : "false", g_native.hooksComplete ? "true" : "false",
            g_native.steeringReplacement ? "loader replacement" : "preserving observer");
    fprintf(file, "  \"native_state\":{\"initial_driver\":%d,\"final_driver\":%d,\"saw_driver_11\":%s,\"saw_driver_12\":%s,\"initial_check\":%d,\"final_check\":%d,\"saw_check_2\":%s},\n",
            g_native.initialDriverState, g_native.finalDriverState, g_native.sawDriver11 ? "true" : "false",
            g_native.sawDriver12 ? "true" : "false", g_native.initialCheckState, g_native.finalCheckState,
            g_native.sawCheck2 ? "true" : "false");
    fprintf(file, "  \"pipeline\":[\n");
    for (unsigned i = 0; i < PIPELINE_COUNT; ++i)
        fprintf(file, "    {\"name\":\"%s\",\"count\":%llu,\"first_ns\":%" PRIu64 ",\"last_ns\":%" PRIu64 "}%s\n",
                names[i], (unsigned long long)atomic_load(&g_native.counts[i]), g_native.firstTimestamp[i],
                g_native.lastTimestamp[i], i + 1 == PIPELINE_COUNT ? "" : ",");
    fprintf(file, "  ],\n  \"callback_evidence\":{\"main_invoked\":%s,\"check_completed\":%s},\n",
            atomic_load(&g_native.counts[PIPE_CABINET_MAIN]) ? "true" : "false", g_native.sawCheck2 ? "true" : "false");
    fprintf(file, "  \"seven_byte_zero_write\":{\"count\":%llu,\"from_CabinetCtrl_Off\":%llu,\"other_or_unknown\":%llu},\n",
            (unsigned long long)atomic_load(&g_native.sevenByteWrites),
            (unsigned long long)atomic_load(&g_native.sevenByteFromCabinetOff),
            (unsigned long long)atomic_load(&g_native.sevenByteUnknownCaller));
    fprintf(file, "  \"capture_complete\":%s,\n  \"limitations\":\"Callback installation is established by completed check state plus observed CabinetCtrl_Main execution; no callback pointer is modified or read.\"\n}\n",
            g_native.hooksComplete ? "true" : "false");
    fclose(file);
    g_native.enabled = 0;
}

#ifdef AER_NATIVE_ACTIVATION_TESTING
void aerNativeActivationTestObserveState(int driverState, int checkState)
{
    if (!g_native.enabled) return;
    observeStateValues(driverState, checkState);
}
void aerNativeActivationTestCountPipeline(unsigned index) { countPipeline(index); }
void aerNativeActivationTestObserveSevenByte(int fromCabinetOff)
{
    if (!g_native.enabled) return;
    atomic_fetch_add(&g_native.sevenByteWrites, 1);
    atomic_fetch_add(fromCabinetOff ? &g_native.sevenByteFromCabinetOff : &g_native.sevenByteUnknownCaller, 1);
}
#endif
