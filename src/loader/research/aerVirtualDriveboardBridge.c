#include "aerVirtualDriveboardBridge.h"

#include <errno.h>
#include <limits.h>
#include <string.h>

#define AER_LINUX_FIONREAD 0x541bu

typedef struct AerVdbBridgeState
{
    AerVdbBootstrap bootstrap;
    AerVdbTransport transport;
    AerVdbDescriptorRegistry registry;
    int requested;
    int initialized;
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
    if (aerVdbBootstrapEvaluate(&g_bridge.bootstrap, &input) != AER_VDB_BOOTSTRAP_ELIGIBLE)
        return g_bridge.bootstrap.result;
    aerVdbTransportInit(&g_bridge.transport, request->boardCount);
    aerVdbRegistryInit(&g_bridge.registry, &g_bridge.transport);
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
    return aerVdbTransportRead(&g_bridge.transport, buffer, size);
}

ssize_t aerVdbBridgeWrite(int fd, const void *buffer, size_t size)
{
    if (!aerVdbBridgeContains(fd)) { errno = EBADF; return -1; }
    return aerVdbTransportWrite(&g_bridge.transport, buffer, size);
}

ssize_t aerVdbBridgeWritev(int fd, const void *const *buffers, const size_t *sizes, size_t count)
{
    if (!aerVdbBridgeContains(fd)) { errno = EBADF; return -1; }
    return aerVdbTransportWritev(&g_bridge.transport, buffers, sizes, count);
}

size_t aerVdbBridgeFwrite(int fd, const void *buffer, size_t elementSize, size_t elementCount)
{
    if (!aerVdbBridgeContains(fd)) { errno = EBADF; return 0; }
    return aerVdbTransportFwrite(&g_bridge.transport, buffer, elementSize, elementCount);
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
void aerVdbBridgeDisconnect(void) { aerVdbTransportDisconnect(&g_bridge.transport); }

void aerVdbBridgeShutdown(void)
{
    if (g_bridge.initialized)
        aerVdbTransportShutdown(&g_bridge.transport);
}

#ifdef AER_VDB_TESTING
int aerVdbBridgeQueueTestResponse(uint8_t response)
{
    return aerVdbTransportQueueAssumedResponse(&g_bridge.transport, response);
}
#endif
