#ifndef AER_VIRTUAL_DRIVEBOARD_H
#define AER_VIRTUAL_DRIVEBOARD_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AER_VDB_EXPECTED_REVISION "DVP-0015A"
#define AER_VDB_EXPECTED_CRC32 0x4debd5f0u
#define AER_VDB_EXPECTED_SHA256 "f16fc04d836a2bd8e401d8f987d4fe694fa16e18a9f870da6623d7f884911075"
#define AER_VDB_MAX_BOARDS 2
#define AER_VDB_QUEUE_CAPACITY 8
#define AER_VDB_MAX_FRAME 7
#define AER_VDB_MAX_DESCRIPTORS 8

typedef enum AerVdbLifecycle
{
    AER_VDB_DISABLED,
    AER_VDB_IDLE,
    AER_VDB_INITIALIZING,
    AER_VDB_CONFIGURING,
    AER_VDB_CALIBRATING,
    AER_VDB_READY,
    AER_VDB_SHUTDOWN,
    AER_VDB_FAULT
} AerVdbLifecycle;

typedef enum AerVdbVerifyResult
{
    AER_VDB_VERIFY_OK,
    AER_VDB_VERIFY_DISABLED,
    AER_VDB_VERIFY_WRONG_REVISION,
    AER_VDB_VERIFY_WRONG_CRC,
    AER_VDB_VERIFY_MISSING_EXECUTABLE,
    AER_VDB_VERIFY_WRONG_SHA256,
    AER_VDB_VERIFY_INCOMPLETE_MANIFEST,
    AER_VDB_VERIFY_BYTE_MISMATCH
} AerVdbVerifyResult;

typedef enum AerVdbConfigResult
{
    AER_VDB_CONFIG_OK,
    AER_VDB_CONFIG_DISABLED,
    AER_VDB_CONFIG_SKIP_CONFLICT,
    AER_VDB_CONFIG_EMULATOR_CONFLICT,
    AER_VDB_CONFIG_PHYSICAL_CONFLICT,
    AER_VDB_CONFIG_UNSUPPORTED_TARGET,
    AER_VDB_CONFIG_INVALID_BOARD_COUNT,
    AER_VDB_CONFIG_INCOMPLETE_VERIFICATION
} AerVdbConfigResult;

typedef struct AerVdbManifestEntry
{
    uint32_t address;
    const uint8_t *expected;
    const uint8_t *actual;
    size_t size;
    int complete;
} AerVdbManifestEntry;

typedef struct AerVdbTarget
{
    int requested;
    const char *revision;
    uint32_t crc32;
    const char *executablePath;
    const char *testExpectedSha256;
    const AerVdbManifestEntry *manifest;
    size_t manifestCount;
} AerVdbTarget;

typedef struct AerVdbConfig
{
    int requested;
    int skipCabinetCheck;
    int emulateDriveboard;
    int physicalSerialRequested;
    int boardCount;
    AerVdbVerifyResult verification;
} AerVdbConfig;

typedef struct AerVdbFrame
{
    uint8_t bytes[AER_VDB_MAX_FRAME];
    size_t size;
} AerVdbFrame;

typedef struct AerVdbTransport
{
    AerVdbLifecycle lifecycle;
    int boardCount;
    int disconnected;
    AerVdbFrame requests[AER_VDB_QUEUE_CAPACITY];
    size_t requestHead, requestCount;
    uint8_t responses[AER_VDB_QUEUE_CAPACITY * AER_VDB_MAX_BOARDS];
    size_t responseHead, responseCount;
    uint8_t partial[AER_VDB_MAX_FRAME];
    size_t partialCount;
    unsigned timeoutTicks;
    int sensorPosition;
    int sensorCenter;
    int sensorTarget;
    int sensorStep;
    unsigned calibrationTicks;
    int physicalOutputAccessed;
} AerVdbTransport;

typedef struct AerVdbDescriptorRegistry
{
    struct { int fd; unsigned generation; int inUse; } slots[AER_VDB_MAX_DESCRIPTORS];
    unsigned nextGeneration;
    size_t openCount;
    AerVdbTransport *transport;
} AerVdbDescriptorRegistry;

int aerVdbSha256File(const char *path, char output[65]);
AerVdbVerifyResult aerVdbVerifyTarget(const AerVdbTarget *target);
AerVdbConfigResult aerVdbValidateConfig(const AerVdbConfig *config);

void aerVdbTransportInit(AerVdbTransport *transport, int boardCount);
int aerVdbTransportStart(AerVdbTransport *transport);
ssize_t aerVdbTransportWrite(AerVdbTransport *transport, const void *data, size_t size);
ssize_t aerVdbTransportWritev(AerVdbTransport *transport, const void *const *data,
                              const size_t *sizes, size_t count);
ssize_t aerVdbTransportRead(AerVdbTransport *transport, void *data, size_t size);
size_t aerVdbTransportFwrite(AerVdbTransport *transport, const void *data,
                             size_t elementSize, size_t elementCount);
size_t aerVdbTransportReadable(const AerVdbTransport *transport);
int aerVdbTransportQueueAssumedResponse(AerVdbTransport *transport, uint8_t response);
int aerVdbTransportTick(AerVdbTransport *transport);
int aerVdbSensorRequest(AerVdbTransport *transport, int direction);
int aerVdbSensorTick(AerVdbTransport *transport);
void aerVdbTransportDisconnect(AerVdbTransport *transport);
void aerVdbTransportShutdown(AerVdbTransport *transport);

void aerVdbRegistryInit(AerVdbDescriptorRegistry *registry, AerVdbTransport *transport);
int aerVdbRegistryOpen(AerVdbDescriptorRegistry *registry, int fd);
int aerVdbRegistryLookup(const AerVdbDescriptorRegistry *registry, int fd);
int aerVdbRegistryDup(AerVdbDescriptorRegistry *registry, int sourceFd, int newFd);
int aerVdbRegistryClose(AerVdbDescriptorRegistry *registry, int fd);

#ifdef __cplusplus
}
#endif
#endif
