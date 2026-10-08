#include "aerVirtualDriveboardBridge.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#ifndef AER_VDB_TESTING
#include "aerActivationDiagnostics.h"
#include "aerDriveboardRecorder.h"
#include "../hardware/lindbergh/driveBoard.h"
#endif

#define AER_LINUX_FIONREAD 0x541bu

typedef struct AerVdbBridgeState
{
    AerVdbBootstrap bootstrap;
    AerVdbTransport transport;
    AerVdbDescriptorRegistry registry;
    int requested;
    int initialized;
    char statusPath[1024];
    AerVdbBootstrapResult result;
} AerVdbBridgeState;

static AerVdbBridgeState g_bridge;

void aerVdbBridgeReset(void)
{
    memset(&g_bridge, 0, sizeof(g_bridge));
}

AerVdbBootstrapResult aerVdbBridgeInitialize(const AerVdbBridgeRequest *request)
{
    AerVdbBootstrapInput input;
    if (!request)
        return AER_VDB_BOOTSTRAP_CONFIG_REJECTED;
    if (g_bridge.initialized)
        return AER_VDB_BOOTSTRAP_ALREADY_EVALUATED;
    g_bridge.initialized = 1;
    g_bridge.requested = request->requested != 0;
    const char *statusPath = getenv("AER_VIRTUAL_DRIVEBOARD_STATUS_OUTPUT");
#ifdef AER_VDB_TESTING
    snprintf(g_bridge.statusPath, sizeof(g_bridge.statusPath), "%s",
             statusPath && statusPath[0] ? statusPath : "");
#else
    snprintf(g_bridge.statusPath, sizeof(g_bridge.statusPath), "%s",
             statusPath && statusPath[0] ? statusPath : "aer_virtual_driveboard_status.json");
#endif
    memset(&input, 0, sizeof(input));
    input.requested = request->requested;
    input.revision = request->revision;
    input.crc32 = request->crc32;
    input.executablePath = request->executablePath;
    input.skipCabinetCheck = request->skipCabinetCheck;
    input.emulateDriveboard = request->emulateDriveboard;
    input.physicalSerialRequested = request->physicalSerialRequested;
    input.boardCount = request->boardCount;
    input.bridgeCapabilities = request->capabilities;
#ifdef AER_VDB_TESTING
    input.testExpectedSha256 = request->testExpectedSha256;
#endif
    g_bridge.result = aerVdbBootstrapEvaluate(&g_bridge.bootstrap, &input);
    if (g_bridge.result != AER_VDB_BOOTSTRAP_ELIGIBLE)
        return g_bridge.result;
    aerVdbTransportInit(&g_bridge.transport, request->boardCount);
    aerVdbTransportEnableNativePolicy(&g_bridge.transport, request->sensorWriter,
                                      request->sensorWriterContext);
    aerVdbRegistryInit(&g_bridge.registry, &g_bridge.transport);
#if !defined(__linux__)
    atexit(aerVdbBridgeShutdown);
#endif
    return g_bridge.bootstrap.result;
}

int aerVdbBridgeRequested(void) { return g_bridge.requested; }
int aerVdbBridgeEligible(void) { return g_bridge.bootstrap.eligible; }

int aerVdbBridgeAttach(int fd)
{
    if (!aerVdbBridgeEligible() || fd < 0 || g_bridge.registry.openCount != 0)
        return 0;
    if (!aerVdbRegistryOpen(&g_bridge.registry, fd))
        return 0;
    if (!aerVdbTransportStart(&g_bridge.transport))
    {
        aerVdbRegistryClose(&g_bridge.registry, fd);
        return 0;
    }
    return 1;
}

int aerVdbBridgeContains(int fd) { return aerVdbRegistryLookup(&g_bridge.registry, fd) >= 0; }
int aerVdbBridgeDup(int sourceFd, int destinationFd) { return aerVdbRegistryDup(&g_bridge.registry, sourceFd, destinationFd); }
int aerVdbBridgeClose(int fd) { return aerVdbRegistryClose(&g_bridge.registry, fd); }

ssize_t aerVdbBridgeRead(int fd, void *buffer, size_t size)
{
    if (!aerVdbBridgeContains(fd)) { errno = EBADF; return -1; }
    uint64_t timestamp = 0;
#ifndef AER_VDB_TESTING
    timestamp = aerDriveboardRecorderMonotonicNs();
#endif
    ssize_t result = aerVdbTransportRead(&g_bridge.transport, buffer, size);
#ifndef AER_VDB_TESTING
    if (aerDriveboardRecorderEnabled())
        aerDriveboardRecorderCaptureRead(timestamp, AER_DRIVEBOARD_ENDPOINT_SERIAL0,
                                         fd, buffer, size, result);
#else
    (void)timestamp;
#endif
    return result;
}

ssize_t aerVdbBridgeWrite(int fd, const void *buffer, size_t size)
{
    if (!aerVdbBridgeContains(fd)) { errno = EBADF; return -1; }
#ifndef AER_VDB_TESTING
    AerDriveboardPendingWrite pending;
    driveboardObserveWriteContext(aerDriveboardRecorderMonotonicNs(),
                                  AER_DRIVEBOARD_ENDPOINT_SERIAL0,
                                  AER_DRIVEBOARD_EVENT_WRITE, size);
    aerDriveboardRecorderPrepareWrite(&pending, AER_DRIVEBOARD_ENDPOINT_SERIAL0,
                                      fd, buffer, size);
#endif
    ssize_t result = aerVdbTransportWrite(&g_bridge.transport, buffer, size);
#ifndef AER_VDB_TESTING
    aerActivationDiagnosticsFirstWriteResult(result);
    aerDriveboardRecorderCompleteWrite(&pending, result);
#endif
    return result;
}

ssize_t aerVdbBridgeWritev(int fd, const void *const *buffers, const size_t *sizes, size_t count)
{
    if (!aerVdbBridgeContains(fd)) { errno = EBADF; return -1; }
    if ((!buffers && count) || (!sizes && count)) { errno = EINVAL; return -1; }
    size_t requested = 0;
#ifndef AER_VDB_TESTING
    uint8_t captured[AER_DRIVEBOARD_MAX_EVENT_BYTES];
    size_t capturedSize = 0;
    AerDriveboardPendingWrite pending;
    for (size_t i = 0; i < count; ++i) {
        requested = SIZE_MAX - requested < sizes[i] ? SIZE_MAX : requested + sizes[i];
        size_t available = sizeof(captured) - capturedSize;
        size_t copy = sizes[i] < available ? sizes[i] : available;
        if (copy && buffers[i]) memcpy(captured + capturedSize, buffers[i], copy);
        capturedSize += copy;
    }
    driveboardObserveWriteContext(aerDriveboardRecorderMonotonicNs(),
                                  AER_DRIVEBOARD_ENDPOINT_SERIAL0,
                                  AER_DRIVEBOARD_EVENT_WRITEV, requested);
    aerDriveboardRecorderPrepareWritePath(&pending, AER_DRIVEBOARD_EVENT_WRITEV,
                                          AER_DRIVEBOARD_ENDPOINT_SERIAL0, fd,
                                          captured, capturedSize);
    pending.requestedCount = requested;
    if (requested > capturedSize) pending.captureStatus = 1;
#else
    (void)requested;
#endif
    ssize_t result = aerVdbTransportWritev(&g_bridge.transport, buffers, sizes, count);
#ifndef AER_VDB_TESTING
    aerActivationDiagnosticsFirstWriteResult(result);
    aerDriveboardRecorderCompleteWrite(&pending, result);
#endif
    return result;
}

size_t aerVdbBridgeFwrite(int fd, const void *buffer, size_t elementSize, size_t elementCount)
{
    if (!aerVdbBridgeContains(fd)) { errno = EBADF; return 0; }
#ifndef AER_VDB_TESTING
    size_t requested = (elementSize && elementCount > SIZE_MAX / elementSize) ? SIZE_MAX : elementSize * elementCount;
    AerDriveboardPendingWrite pending;
    driveboardObserveWriteContext(aerDriveboardRecorderMonotonicNs(),
                                  AER_DRIVEBOARD_ENDPOINT_SERIAL0,
                                  AER_DRIVEBOARD_EVENT_FWRITE, requested);
    aerDriveboardRecorderPrepareWritePath(&pending, AER_DRIVEBOARD_EVENT_FWRITE,
                                          AER_DRIVEBOARD_ENDPOINT_SERIAL0, fd,
                                          buffer, requested);
#endif
    size_t result = aerVdbTransportFwrite(&g_bridge.transport, buffer, elementSize, elementCount);
#ifndef AER_VDB_TESTING
    aerActivationDiagnosticsFirstWriteResult((int64_t)result);
    aerDriveboardRecorderCompleteWrite(&pending, (ssize_t)result);
#endif
    return result;
}

int aerVdbBridgeIoctl(int fd, unsigned long request, void *argument)
{
    size_t readable;
    if (!aerVdbBridgeContains(fd)) { errno = EBADF; return -1; }
    if (request != AER_LINUX_FIONREAD || !argument) { errno = EINVAL; return -1; }
    readable = aerVdbTransportReadable(&g_bridge.transport);
    if (readable > INT_MAX) readable = INT_MAX;
    *(int *)argument = (int)readable;
    return 0;
}

int aerVdbBridgeReadable(int fd)
{
    return aerVdbBridgeContains(fd) && aerVdbTransportReadable(&g_bridge.transport) != 0;
}

int aerVdbBridgeWritable(int fd)
{
    return aerVdbBridgeContains(fd) && g_bridge.transport.lifecycle != AER_VDB_FAULT &&
           g_bridge.transport.lifecycle != AER_VDB_SHUTDOWN &&
           g_bridge.transport.requestCount < AER_VDB_QUEUE_CAPACITY;
}

int aerVdbBridgeTick(void) { return aerVdbTransportTick(&g_bridge.transport); }
AerVdbLifecycle aerVdbBridgeLifecycle(void) { return g_bridge.transport.lifecycle; }
uint64_t aerVdbBridgeAcceptedFrames(void) { return g_bridge.transport.acceptedFrames; }
uint64_t aerVdbBridgeNativeCommandFrames(void) { return g_bridge.transport.nativeCommandFrames; }
int aerVdbBridgePhysicalOutputAccessed(void) { return g_bridge.transport.physicalOutputAccessed; }
void aerVdbBridgeDisconnect(void) { aerVdbTransportDisconnect(&g_bridge.transport); }

void aerVdbBridgeShutdown(void)
{
    if (g_bridge.initialized)
    {
        AerVdbLifecycle finalLifecycle = g_bridge.transport.lifecycle;
        uint64_t acceptedFrames = g_bridge.transport.acceptedFrames;
        uint64_t nativeFrames = g_bridge.transport.nativeCommandFrames;
        int physicalOutputAccessed = g_bridge.transport.physicalOutputAccessed;
        aerVdbTransportShutdown(&g_bridge.transport);
        if (g_bridge.requested && g_bridge.statusPath[0])
        {
            FILE *status = fopen(g_bridge.statusPath, "wb");
            if (status)
            {
                fprintf(status,
                        "{\n  \"schema\":\"AER_VIRTUAL_DRIVEBOARD_STATUS_V1\",\n"
                        "  \"bootstrap_result\":%d,\n  \"eligible\":%s,\n"
                        "  \"final_lifecycle\":%d,\n  \"accepted_frames\":%llu,\n"
                        "  \"native_command_frames\":%llu,\n"
                        "  \"physical_output_accessed\":%s,\n"
                        "  \"physical_isolation\":%s\n}\n",
                        g_bridge.result, g_bridge.bootstrap.eligible ? "true" : "false",
                        finalLifecycle, (unsigned long long)acceptedFrames,
                        (unsigned long long)nativeFrames,
                        physicalOutputAccessed ? "true" : "false",
                        physicalOutputAccessed ? "false" : "true");
                fclose(status);
            }
        }
    }
}

#ifdef AER_VDB_TESTING
int aerVdbBridgeQueueTestResponse(uint8_t response)
{
    return aerVdbTransportQueueAssumedResponse(&g_bridge.transport, response);
}
#endif
