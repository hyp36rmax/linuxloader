#include "aerActivationDiagnostics.h"
#include "aerNativeActivation.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../patching/flowControl.h"
#ifndef AER_ACTIVATION_DIAGNOSTICS_TESTING
#include "../../minhook/include/MinHook.h"
#else
typedef int MH_STATUS;
#define MH_OK 0
#define MH_ERROR_ALREADY_CREATED 3
extern MH_STATUS MH_CreateHook(void *target, void *replacement, void **original);
#endif

#define MAX_PATCHES 8
#define MAX_HOOKS 4
#define MAX_BYTES 16

typedef struct {
    size_t address;
    char label[48];
    char before[MAX_BYTES * 2 + 1];
    char requested[MAX_BYTES * 2 + 1];
    char after[MAX_BYTES * 2 + 1];
    int protectionResult;
    int verified;
} PatchRecord;

typedef struct {
    size_t address;
    char label[48];
    int creationResult;
    atomic_ullong invocations;
} HookRecord;

typedef struct {
    int initialized;
    int enabled;
    char revision[32];
    char outputPath[1024];
    PatchRecord patches[MAX_PATCHES];
    unsigned patchCount;
    HookRecord hooks[MAX_HOOKS];
    unsigned hookCount;
    int hookEnableResult;
    atomic_ullong selectReadable;
    atomic_ullong ioctlReadable;
    atomic_ullong ioctlNotReadable;
    atomic_ullong reads;
    atomic_ullong readsEnableFalse;
    atomic_ullong readsEnableTrue;
    atomic_ullong readsWithoutResponse;
    atomic_ullong responseTransitions;
    atomic_int firstWriteSeen;
    uint64_t firstWriteTimestamp;
    int firstWriteEndpoint;
    int firstWriteApi;
    size_t firstWriteLength;
    int firstWriteInitialized;
    int firstWriteEnableRead;
    uint8_t firstWriteResponse;
    int firstWriteResultSet;
    int64_t firstWriteResult;
} DiagnosticState;

static DiagnosticState g_diag;

static int envEnabled(void)
{
    const char *value = getenv("AER_ACTIVATION_DIAGNOSTICS");
    return value && (!strcmp(value, "1") || !strcmp(value, "true") || !strcmp(value, "TRUE"));
}

static void hexBytes(char *out, size_t outSize, const void *data, size_t length)
{
    const unsigned char *bytes = (const unsigned char *)data;
    size_t used = 0;
    for (size_t i = 0; i < length && used + 2 < outSize; ++i)
        used += (size_t)snprintf(out + used, outSize - used, "%02X", bytes[i]);
}

static size_t parseHex(const char *text, unsigned char *bytes, size_t capacity)
{
    size_t length = strlen(text) / 2;
    if (length > capacity)
        length = capacity;
    for (size_t i = 0; i < length; ++i) {
        char pair[3] = {text[i * 2], text[i * 2 + 1], 0};
        bytes[i] = (unsigned char)strtoul(pair, NULL, 16);
    }
    return length;
}

void aerActivationDiagnosticsInitialize(const char *revision)
{
    if (g_diag.initialized)
        return;
    memset(&g_diag, 0, sizeof(g_diag));
    g_diag.initialized = 1;
    if (!envEnabled() || !revision || strcmp(revision, "DVP-0015A"))
        return;
    g_diag.enabled = 1;
    snprintf(g_diag.revision, sizeof(g_diag.revision), "%s", revision);
    const char *path = getenv("AER_ACTIVATION_DIAGNOSTICS_OUTPUT");
    snprintf(g_diag.outputPath, sizeof(g_diag.outputPath), "%s", path && path[0] ? path : "aer_activation_diagnostics.json");
    atomic_init(&g_diag.selectReadable, 0);
    atomic_init(&g_diag.ioctlReadable, 0);
    atomic_init(&g_diag.ioctlNotReadable, 0);
    atomic_init(&g_diag.reads, 0);
    atomic_init(&g_diag.readsEnableFalse, 0);
    atomic_init(&g_diag.readsEnableTrue, 0);
    atomic_init(&g_diag.readsWithoutResponse, 0);
    atomic_init(&g_diag.responseTransitions, 0);
    atomic_init(&g_diag.firstWriteSeen, 0);
#if !defined(__linux__)
    atexit(aerActivationDiagnosticsShutdown);
#endif
}

int aerActivationDiagnosticsEnabled(void) { return g_diag.enabled; }
int aerActivationDiagnosticsUsesAtexit(void)
{
#if defined(__linux__)
    return 0;
#else
    return 1;
#endif
}

void aerActivationDiagnosticsPatch(size_t address, const char *replacement, const char *label)
{
    if (!g_diag.enabled) {
        patchMemoryFromStringResult(address, replacement);
        return;
    }
    unsigned char expected[MAX_BYTES] = {0};
    size_t length = parseHex(replacement, expected, sizeof(expected));
    PatchRecord *record = g_diag.patchCount < MAX_PATCHES ? &g_diag.patches[g_diag.patchCount++] : NULL;
    if (record) {
        record->address = address;
        snprintf(record->label, sizeof(record->label), "%s", label);
        hexBytes(record->before, sizeof(record->before), (const void *)address, length);
        hexBytes(record->requested, sizeof(record->requested), expected, length);
    }
    int result = patchMemoryFromStringResult(address, replacement);
    if (record) {
        record->protectionResult = result;
        hexBytes(record->after, sizeof(record->after), (const void *)address, length);
        record->verified = result == 0 && !memcmp((const void *)address, expected, length);
    }
}

static void recordHook(size_t address, const char *label, int result)
{
    HookRecord *record = NULL;
    for (unsigned i = 0; i < g_diag.hookCount; ++i)
        if (g_diag.hooks[i].address == address) record = &g_diag.hooks[i];
    if (!record && g_diag.hookCount < MAX_HOOKS) {
        record = &g_diag.hooks[g_diag.hookCount++];
        record->address = address;
        record->creationResult = -1;
        snprintf(record->label, sizeof(record->label), "%s", label);
        atomic_init(&record->invocations, 0);
    }
    if (record && (record->creationResult != MH_OK || result == MH_OK) && result != MH_ERROR_ALREADY_CREATED)
        record->creationResult = result;
}

void aerActivationDiagnosticsCreateReturnOneHook(size_t address, const char *label)
{
    void *replacement = address == 0x08103eaa ? (void *)aerActivationDiagnosticsSteeringHook : (void *)aerActivationDiagnosticsActuatorHook;
    int result = MH_CreateHook((void *)address, replacement, NULL);
    if (g_diag.enabled) recordHook(address, label, result);
}

void aerActivationDiagnosticsHooksEnabled(int result) { if (g_diag.enabled) g_diag.hookEnableResult = result; }

static int invoked(size_t address)
{
    if (g_diag.enabled)
        for (unsigned i = 0; i < g_diag.hookCount; ++i)
            if (g_diag.hooks[i].address == address) atomic_fetch_add(&g_diag.hooks[i].invocations, 1);
    return 1;
}

int aerActivationDiagnosticsSteeringHook(void)
{
    int result = invoked(0x08103eaa);
    aerNativeActivationObserveInitReplacement(result);
    return result;
}
int aerActivationDiagnosticsActuatorHook(void) { return invoked(0x08105d88); }
void aerActivationDiagnosticsSelectReadable(void) { if (g_diag.enabled) atomic_fetch_add(&g_diag.selectReadable, 1); }
void aerActivationDiagnosticsIoctl(int readable, uint8_t response) { (void)response; if (g_diag.enabled) atomic_fetch_add(readable ? &g_diag.ioctlReadable : &g_diag.ioctlNotReadable, 1); }
void aerActivationDiagnosticsRead(int before, int wrote, uint8_t response) { (void)response; if (!g_diag.enabled) return; atomic_fetch_add(&g_diag.reads, 1); atomic_fetch_add(before ? &g_diag.readsEnableTrue : &g_diag.readsEnableFalse, 1); if (!wrote) atomic_fetch_add(&g_diag.readsWithoutResponse, 1); }
void aerActivationDiagnosticsResponseTransition(uint8_t before, uint8_t after) { if (g_diag.enabled && before != after) atomic_fetch_add(&g_diag.responseTransitions, 1); }

void aerActivationDiagnosticsFirstWrite(uint64_t timestamp, int endpoint, int api, size_t length, int initialized, int enableRead, uint8_t response)
{
    if (!g_diag.enabled || atomic_exchange(&g_diag.firstWriteSeen, 1)) return;
    g_diag.firstWriteTimestamp = timestamp;
    g_diag.firstWriteEndpoint = endpoint;
    g_diag.firstWriteApi = api;
    g_diag.firstWriteLength = length;
    g_diag.firstWriteInitialized = initialized;
    g_diag.firstWriteEnableRead = enableRead;
    g_diag.firstWriteResponse = response;
}

void aerActivationDiagnosticsFirstWriteResult(int64_t result)
{
    if (!g_diag.enabled || !atomic_load(&g_diag.firstWriteSeen) || g_diag.firstWriteResultSet) return;
    g_diag.firstWriteResult = result;
    g_diag.firstWriteResultSet = 1;
}

void aerActivationDiagnosticsShutdown(void)
{
    if (!g_diag.enabled) return;
    FILE *file = fopen(g_diag.outputPath, "wb");
    if (!file) return;
    fprintf(file, "{\n  \"schema\": \"%s\",\n  \"game_revision\": \"%s\",\n", AER_ACTIVATION_DIAGNOSTIC_VERSION, g_diag.revision);
    fprintf(file, "  \"expected_original_bytes\": \"UNKNOWN\",\n  \"patches\": [\n");
    for (unsigned i = 0; i < g_diag.patchCount; ++i) {
        PatchRecord *p = &g_diag.patches[i];
        fprintf(file, "    {\"label\":\"%s\",\"address\":\"0x%08" PRIxPTR "\",\"expected_original\":\"UNKNOWN\",\"before\":\"%s\",\"requested\":\"%s\",\"after\":\"%s\",\"memory_protection_result\":\"%s\",\"application_result\":%d,\"verified\":%s}%s\n", p->label, (uintptr_t)p->address, p->before, p->requested, p->after, p->protectionResult == -2 ? "failure" : (p->protectionResult == -1 ? "not-attempted" : "success"), p->protectionResult, p->verified ? "true" : "false", i + 1 == g_diag.patchCount ? "" : ",");
    }
    fprintf(file, "  ],\n  \"hooks\": [\n");
    for (unsigned i = 0; i < g_diag.hookCount; ++i) {
        HookRecord *h = &g_diag.hooks[i];
        fprintf(file, "    {\"label\":\"%s\",\"address\":\"0x%08" PRIxPTR "\",\"creation_result\":%d,\"enable_result\":%d,\"invocations\":%llu}%s\n", h->label, (uintptr_t)h->address, h->creationResult, g_diag.hookEnableResult, (unsigned long long)atomic_load(&h->invocations), i + 1 == g_diag.hookCount ? "" : ",");
    }
    fprintf(file, "  ],\n  \"readiness\": {\"select_readable\":%llu,\"ioctl_readable\":%llu,\"ioctl_not_readable\":%llu,\"reads\":%llu,\"reads_enable_false\":%llu,\"reads_enable_true\":%llu,\"reads_without_valid_response\":%llu,\"response_transitions\":%llu},\n", (unsigned long long)atomic_load(&g_diag.selectReadable), (unsigned long long)atomic_load(&g_diag.ioctlReadable), (unsigned long long)atomic_load(&g_diag.ioctlNotReadable), (unsigned long long)atomic_load(&g_diag.reads), (unsigned long long)atomic_load(&g_diag.readsEnableFalse), (unsigned long long)atomic_load(&g_diag.readsEnableTrue), (unsigned long long)atomic_load(&g_diag.readsWithoutResponse), (unsigned long long)atomic_load(&g_diag.responseTransitions));
    fprintf(file, "  \"first_write\": {\"observed\":%s,\"timestamp_ns\":%" PRIu64 ",\"endpoint\":%d,\"write_api\":%d,\"requested_length\":%zu,\"result_observed\":%s,\"actual_result\":%" PRId64 ",\"wheel_initialized\":%d,\"enable_read\":%d,\"response\":%u},\n", atomic_load(&g_diag.firstWriteSeen) ? "true" : "false", g_diag.firstWriteTimestamp, g_diag.firstWriteEndpoint, g_diag.firstWriteApi, g_diag.firstWriteLength, g_diag.firstWriteResultSet ? "true" : "false", g_diag.firstWriteResult, g_diag.firstWriteInitialized, g_diag.firstWriteEnableRead, g_diag.firstWriteResponse);
    fprintf(file, "  \"capture_complete\": true,\n  \"limitations\": \"Original bytes and original executable control flow are not independently known\"\n}\n");
    fclose(file);
    g_diag.enabled = 0;
}

#ifdef AER_ACTIVATION_DIAGNOSTICS_TESTING
void aerActivationDiagnosticsTestHookResult(size_t address, const char *label, int result) { recordHook(address, label, result); }
#endif
