#ifndef AER_VIRTUAL_DRIVEBOARD_BRIDGE_H
#define AER_VIRTUAL_DRIVEBOARD_BRIDGE_H

#include "aerVirtualDriveboardBootstrap.h"

#include <stdio.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AerVdbBridgeRequest
{
    int requested;
    const char *revision;
    uint32_t crc32;
    const char *executablePath;
    int skipCabinetCheck;
    int emulateDriveboard;
    int physicalSerialRequested;
    int boardCount;
    uint32_t capabilities;
    AerVdbSensorWriter sensorWriter;
    void *sensorWriterContext;
#ifdef AER_VDB_TESTING
    const char *testExpectedSha256;
#endif
} AerVdbBridgeRequest;

void aerVdbBridgeReset(void);
AerVdbBootstrapResult aerVdbBridgeInitialize(const AerVdbBridgeRequest *request);
int aerVdbBridgeRequested(void);
int aerVdbBridgeEligible(void);
int aerVdbBridgeAttach(int fd);
int aerVdbBridgeContains(int fd);
int aerVdbBridgeDup(int sourceFd, int destinationFd);
int aerVdbBridgeClose(int fd);
ssize_t aerVdbBridgeRead(int fd, void *buffer, size_t size);
ssize_t aerVdbBridgeWrite(int fd, const void *buffer, size_t size);
ssize_t aerVdbBridgeWritev(int fd, const void *const *buffers, const size_t *sizes, size_t count);
size_t aerVdbBridgeFwrite(int fd, const void *buffer, size_t elementSize, size_t elementCount);
int aerVdbBridgeIoctl(int fd, unsigned long request, void *argument);
int aerVdbBridgeReadable(int fd);
int aerVdbBridgeWritable(int fd);
int aerVdbBridgeTick(void);
AerVdbLifecycle aerVdbBridgeLifecycle(void);
uint64_t aerVdbBridgeAcceptedFrames(void);
uint64_t aerVdbBridgeNativeCommandFrames(void);
int aerVdbBridgePhysicalOutputAccessed(void);
void aerVdbBridgeDisconnect(void);
void aerVdbBridgeShutdown(void);

#ifdef AER_VDB_TESTING
int aerVdbBridgeQueueTestResponse(uint8_t response);
#endif

#ifdef __cplusplus
}
#endif
#endif
