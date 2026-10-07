#ifndef AER_DRIVEBOARD_RECORDER_H
#define AER_DRIVEBOARD_RECORDER_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define AER_DRIVEBOARD_SCHEMA "AER_DRIVEBOARD_RAW_V1"
#define AER_DRIVEBOARD_MAX_EVENT_BYTES 4096u

typedef struct
{
    const char *gameRevision;
    const char *gameExecutableHash;
    const char *cabinetType;
    const char *cabinetId;
    int emulateDriveboard;
    int skipCabinetCheck;
} AerDriveboardRecorderMetadata;

typedef enum
{
    AER_DRIVEBOARD_ENDPOINT_UNKNOWN = 0,
    AER_DRIVEBOARD_ENDPOINT_SERIAL0 = 1,
    AER_DRIVEBOARD_ENDPOINT_SERIAL1 = 2
} AerDriveboardEndpoint;

typedef struct
{
    uint64_t timestampNs;
    AerDriveboardEndpoint endpoint;
    int fileDescriptor;
    size_t requestedCount;
    size_t capturedCount;
    uint16_t captureStatus;
    int active;
    uint8_t bytes[AER_DRIVEBOARD_MAX_EVENT_BYTES];
} AerDriveboardPendingWrite;

void aerDriveboardRecorderInitialize(const AerDriveboardRecorderMetadata *metadata);
void aerDriveboardRecorderShutdown(void);
int aerDriveboardRecorderEnabled(void);
uint64_t aerDriveboardRecorderMonotonicNs(void);
void aerDriveboardRecorderPrepareWrite(AerDriveboardPendingWrite *pending, AerDriveboardEndpoint endpoint,
                                       int fd, const void *bytes, size_t requestedCount);
void aerDriveboardRecorderCompleteWrite(AerDriveboardPendingWrite *pending, ssize_t result);
void aerDriveboardRecorderCaptureWrite(uint64_t timestampNs, AerDriveboardEndpoint endpoint, int fd,
                                       const void *bytes, size_t requestedCount, ssize_t result);
void aerDriveboardRecorderCaptureRead(uint64_t timestampNs, AerDriveboardEndpoint endpoint, int fd,
                                      const void *bytes, size_t requestedCount, ssize_t result);
void aerDriveboardRecorderMark(const char *marker);

#ifdef AER_RECORDER_TESTING
void aerDriveboardRecorderTestPauseWriter(int paused);
#endif

#endif
