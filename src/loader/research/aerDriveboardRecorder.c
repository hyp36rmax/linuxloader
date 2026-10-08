#include "aerDriveboardRecorder.h"

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <process.h>
#define aer_getpid _getpid
#define AER_OS_NAME "Windows"
#else
#include <unistd.h>
#define aer_getpid getpid
#if defined(__APPLE__)
#define AER_OS_NAME "macOS"
#else
#define AER_OS_NAME "Linux"
#endif
#endif

#define AER_RECORDER_VERSION "1"
#define AER_LOADER_REPOSITORY "https://github.com/lindbergh-loader/linuxloader"
#define AER_LOADER_BASELINE "9aa6e3e45ccfbbc60eb0f975aa9d3d158a13706c"
#define AER_QUEUE_CAPACITY 256u
#define AER_MAX_EVENT_BYTES AER_DRIVEBOARD_MAX_EVENT_BYTES
#define AER_MAX_MARKER_BYTES 96u
#define AER_PATH_SIZE 1024u

enum
{
    AER_CAPTURE_COMPLETE = 0,
    AER_CAPTURE_TRUNCATED = 1,
    AER_CAPTURE_INVALID_BUFFER = 2
};

#pragma pack(push, 1)
typedef struct
{
    char magic[8];
    uint16_t formatVersion;
    uint16_t headerSize;
    uint32_t endianMarker;
    char schema[32];
} AerBinaryHeader;

typedef struct
{
    uint32_t recordSize;
    uint16_t eventType;
    uint16_t captureStatus;
    uint64_t sequence;
    uint64_t timestampNs;
    uint64_t processId;
    uint64_t threadId;
    int32_t endpoint;
    int32_t fileDescriptor;
    uint64_t requestedCount;
    int64_t operationResult;
    uint32_t payloadLength;
    uint32_t reserved;
} AerRecordHeader;
#pragma pack(pop)

typedef struct
{
    AerRecordHeader header;
    uint8_t payload[AER_MAX_EVENT_BYTES];
} AerQueuedEvent;

typedef struct
{
    int initialized;
    int enabled;
    int stopRequested;
    int writerStarted;
    atomic_int captureIncomplete;
    pthread_t writerThread;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    AerQueuedEvent queue[AER_QUEUE_CAPACITY];
    size_t queueHead;
    size_t queueTail;
    size_t queueCount;
    uint64_t nextSequence;
    atomic_uint droppedRecords;
    uint64_t writtenRecords;
    uint64_t overflowReportedThrough;
    uint64_t startTimestampNs;
    uint64_t endTimestampNs;
    FILE *binaryFile;
    char binaryPath[AER_PATH_SIZE];
    char metadataPath[AER_PATH_SIZE];
    char markerPath[AER_PATH_SIZE];
    char captureId[96];
    long markerOffset;
    AerDriveboardRecorderMetadata metadata;
    char gameRevision[64];
    char executableHash[96];
    char cabinetType[64];
    char cabinetId[64];
#ifdef AER_RECORDER_TESTING
    atomic_int writerPaused;
#endif
} AerRecorderState;

static AerRecorderState g_recorder;

static const char *safeValue(const char *value)
{
    return (value != NULL && value[0] != '\0') ? value : "UNKNOWN";
}

static void copyMetadataValue(char *destination, size_t destinationSize, const char *value)
{
    snprintf(destination, destinationSize, "%s", safeValue(value));
}

uint64_t aerDriveboardRecorderMonotonicNs(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
        return 0;
    return ((uint64_t)value.tv_sec * UINT64_C(1000000000)) + (uint64_t)value.tv_nsec;
}

static uint64_t currentThreadId(void)
{
    pthread_t current = pthread_self();
    uint64_t value = 0;
    size_t copySize = sizeof(current) < sizeof(value) ? sizeof(current) : sizeof(value);
    memcpy(&value, &current, copySize);
    return value;
}

static int environmentEnabled(void)
{
    const char *value = getenv("AER_DRIVEBOARD_RECORDER");
    return value != NULL && (strcmp(value, "1") == 0 || strcmp(value, "true") == 0 || strcmp(value, "TRUE") == 0);
}

static int supportedRevision(const char *revision)
{
    return revision != NULL && (strcmp(revision, "DVP-0015") == 0 || strcmp(revision, "DVP-0015A") == 0);
}

static void jsonString(FILE *file, const char *name, const char *value, int trailingComma)
{
    const unsigned char *cursor = (const unsigned char *)safeValue(value);
    fprintf(file, "  \"%s\": \"", name);
    while (*cursor)
    {
        if (*cursor == '\\' || *cursor == '"')
            fputc('\\', file);
        if (*cursor >= 0x20)
            fputc(*cursor, file);
        ++cursor;
    }
    fprintf(file, "\"%s\n", trailingComma ? "," : "");
}

static void writeMetadata(void)
{
    FILE *file = fopen(g_recorder.metadataPath, "wb");
    if (file == NULL)
        return;

    const char *patches = "none";
    if (g_recorder.metadata.emulateDriveboard && g_recorder.metadata.skipCabinetCheck)
        patches = "driveboard-output-enable; cabinet-check-bypass";
    else if (g_recorder.metadata.emulateDriveboard)
        patches = "driveboard-output-enable";
    else if (g_recorder.metadata.skipCabinetCheck)
        patches = "cabinet-check-bypass";

    fprintf(file, "{\n");
    jsonString(file, "schema", AER_DRIVEBOARD_SCHEMA, 1);
    jsonString(file, "capture_id", g_recorder.captureId, 1);
    jsonString(file, "loader_repository", AER_LOADER_REPOSITORY, 1);
    jsonString(file, "loader_baseline_commit", AER_LOADER_BASELINE, 1);
    jsonString(file, "game_revision", g_recorder.gameRevision, 1);
    jsonString(file, "executable_sha256", g_recorder.executableHash, 1);
    jsonString(file, "operating_system", AER_OS_NAME, 1);
    jsonString(file, "cabinet_type", g_recorder.cabinetType, 1);
    jsonString(file, "configured_cabinet_id", g_recorder.cabinetId, 1);
    fprintf(file, "  \"driveboard_emulation\": %s,\n", g_recorder.metadata.emulateDriveboard ? "true" : "false");
    fprintf(file, "  \"cabinet_check_bypass\": %s,\n", g_recorder.metadata.skipCabinetCheck ? "true" : "false");
    jsonString(file, "relevant_runtime_patches", patches, 1);
    jsonString(file, "recorder_version", AER_RECORDER_VERSION, 1);
    fprintf(file, "  \"start_monotonic_ns\": %" PRIu64 ",\n", g_recorder.startTimestampNs);
    fprintf(file, "  \"end_monotonic_ns\": %" PRIu64 ",\n", g_recorder.endTimestampNs);
    fprintf(file, "  \"records_written\": %" PRIu64 ",\n", g_recorder.writtenRecords);
    fprintf(file, "  \"dropped_record_count\": %u,\n", atomic_load(&g_recorder.droppedRecords));
    fprintf(file, "  \"capture_complete\": %s,\n", atomic_load(&g_recorder.captureIncomplete) ? "false" : "true");
    jsonString(file, "inbound_provenance", "LOADER-SYNTHESIZED RESPONSES", 0);
    fprintf(file, "}\n");
    fclose(file);
}

static void writeEvent(const AerQueuedEvent *event)
{
    if (g_recorder.binaryFile == NULL)
        return;
    if (fwrite(&event->header, sizeof(event->header), 1, g_recorder.binaryFile) != 1)
    {
        atomic_store(&g_recorder.captureIncomplete, 1);
        return;
    }
    if (event->header.payloadLength > 0)
    {
        if (fwrite(event->payload, event->header.payloadLength, 1, g_recorder.binaryFile) != 1)
        {
            atomic_store(&g_recorder.captureIncomplete, 1);
            return;
        }
    }
    ++g_recorder.writtenRecords;
}

static void fillEvent(AerQueuedEvent *event, uint16_t type, uint16_t status, uint64_t timestampNs,
                      AerDriveboardEndpoint endpoint, int fd, size_t requestedCount, ssize_t result,
                      const void *payload, size_t payloadLength)
{
    memset(event, 0, sizeof(*event));
    event->header.eventType = type;
    event->header.captureStatus = status;
    event->header.sequence = 0;
    event->header.timestampNs = timestampNs;
    event->header.processId = (uint64_t)aer_getpid();
    event->header.threadId = currentThreadId();
    event->header.endpoint = (int32_t)endpoint;
    event->header.fileDescriptor = fd;
    event->header.requestedCount = requestedCount;
    event->header.operationResult = result;
    event->header.payloadLength = (uint32_t)payloadLength;
    event->header.recordSize = (uint32_t)(sizeof(event->header) + payloadLength);
    if (payloadLength > 0 && payload != NULL)
        memcpy(event->payload, payload, payloadLength);
}

static int enqueueEvent(const AerQueuedEvent *event)
{
    if (!g_recorder.enabled)
        return 0;

#ifdef AER_RECORDER_TESTING
    int lockResult = pthread_mutex_lock(&g_recorder.mutex);
#else
    int lockResult = pthread_mutex_trylock(&g_recorder.mutex);
#endif
    if (lockResult != 0)
    {
        atomic_fetch_add(&g_recorder.droppedRecords, 1);
        atomic_store(&g_recorder.captureIncomplete, 1);
        return 0;
    }

    if (g_recorder.queueCount == AER_QUEUE_CAPACITY)
    {
        atomic_fetch_add(&g_recorder.droppedRecords, 1);
        atomic_store(&g_recorder.captureIncomplete, 1);
        pthread_mutex_unlock(&g_recorder.mutex);
        return 0;
    }

    g_recorder.queue[g_recorder.queueTail] = *event;
    g_recorder.queue[g_recorder.queueTail].header.sequence = g_recorder.nextSequence++;
    g_recorder.queueTail = (g_recorder.queueTail + 1u) % AER_QUEUE_CAPACITY;
    ++g_recorder.queueCount;
    pthread_cond_signal(&g_recorder.condition);
    pthread_mutex_unlock(&g_recorder.mutex);
    return 1;
}

static int enqueueWriterEvent(const AerQueuedEvent *event)
{
    pthread_mutex_lock(&g_recorder.mutex);
    if (g_recorder.queueCount == AER_QUEUE_CAPACITY)
    {
        pthread_mutex_unlock(&g_recorder.mutex);
        return 0;
    }
    g_recorder.queue[g_recorder.queueTail] = *event;
    g_recorder.queue[g_recorder.queueTail].header.sequence = g_recorder.nextSequence++;
    g_recorder.queueTail = (g_recorder.queueTail + 1u) % AER_QUEUE_CAPACITY;
    ++g_recorder.queueCount;
    pthread_mutex_unlock(&g_recorder.mutex);
    return 1;
}

static void emitPendingOverflowRecord(void)
{
    uint64_t dropped = atomic_load(&g_recorder.droppedRecords);
    uint64_t reported = g_recorder.overflowReportedThrough;
    if (dropped <= reported)
        return;

    AerQueuedEvent event;
    fillEvent(&event, AER_DRIVEBOARD_EVENT_OVERFLOW, AER_CAPTURE_COMPLETE, aerDriveboardRecorderMonotonicNs(),
              AER_DRIVEBOARD_ENDPOINT_UNKNOWN, -1, 0, (ssize_t)(dropped - reported), NULL, 0);
    if (enqueueWriterEvent(&event))
        g_recorder.overflowReportedThrough = dropped;
}

static void pollMarkerFile(void)
{
    if (g_recorder.markerPath[0] == '\0')
        return;

    FILE *file = fopen(g_recorder.markerPath, "rb");
    if (file == NULL)
        return;
    if (fseek(file, g_recorder.markerOffset, SEEK_SET) != 0)
    {
        fclose(file);
        return;
    }

    char line[AER_MAX_MARKER_BYTES + 2u];
    while (fgets(line, sizeof(line), file) != NULL)
    {
        size_t length = strcspn(line, "\r\n");
        line[length] = '\0';
        if (length == 0)
            continue;
        AerQueuedEvent event;
        fillEvent(&event, AER_DRIVEBOARD_EVENT_MARKER, AER_CAPTURE_COMPLETE, aerDriveboardRecorderMonotonicNs(),
                  AER_DRIVEBOARD_ENDPOINT_UNKNOWN, -1, length, (ssize_t)length, line, length);
        enqueueWriterEvent(&event);
    }
    g_recorder.markerOffset = ftell(file);
    fclose(file);
}

static void *writerMain(void *unused)
{
    (void)unused;
    for (;;)
    {
        AerQueuedEvent event;
        int haveEvent = 0;
        int queueEmpty = 0;

        pthread_mutex_lock(&g_recorder.mutex);
        while (g_recorder.queueCount == 0 && !g_recorder.stopRequested)
        {
            struct timespec timeout;
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_nsec += 100000000;
            if (timeout.tv_nsec >= 1000000000)
            {
                ++timeout.tv_sec;
                timeout.tv_nsec -= 1000000000;
            }
            pthread_cond_timedwait(&g_recorder.condition, &g_recorder.mutex, &timeout);
            break;
        }
#ifdef AER_RECORDER_TESTING
        if (!atomic_load(&g_recorder.writerPaused) && g_recorder.queueCount > 0)
#else
        if (g_recorder.queueCount > 0)
#endif
        {
            event = g_recorder.queue[g_recorder.queueHead];
            g_recorder.queueHead = (g_recorder.queueHead + 1u) % AER_QUEUE_CAPACITY;
            --g_recorder.queueCount;
            haveEvent = 1;
        }
        queueEmpty = g_recorder.queueCount == 0;
        pthread_mutex_unlock(&g_recorder.mutex);

        if (haveEvent)
            writeEvent(&event);
        if (queueEmpty)
        {
            emitPendingOverflowRecord();
            pollMarkerFile();
        }
        pthread_mutex_lock(&g_recorder.mutex);
        int shouldStop = g_recorder.stopRequested && g_recorder.queueCount == 0;
        pthread_mutex_unlock(&g_recorder.mutex);
        if (shouldStop)
            break;
    }
    fflush(g_recorder.binaryFile);
    return NULL;
}

static int appendPathSuffix(char *destination, size_t capacity, const char *prefix, const char *suffix)
{
    size_t prefixLength = strlen(prefix);
    size_t suffixLength = strlen(suffix);
    if (capacity == 0 || prefixLength > SIZE_MAX - suffixLength - 1 ||
        prefixLength + suffixLength + 1 > capacity)
        return -1;
    memcpy(destination, prefix, prefixLength);
    memcpy(destination + prefixLength, suffix, suffixLength + 1);
    return 0;
}

static int buildOutputPaths(const char *configuredPrefix, uint64_t timestamp,
                            char *binaryPath, size_t binaryCapacity,
                            char *metadataPath, size_t metadataCapacity)
{
    const char *prefix = configuredPrefix;
    char generated[64];
    if (prefix == NULL || prefix[0] == '\0')
    {
        int written = snprintf(generated, sizeof(generated), "aer_driveboard_%" PRIu64, timestamp);
        if (written < 0 || (size_t)written >= sizeof(generated))
            return -1;
        prefix = generated;
    }
    if (appendPathSuffix(binaryPath, binaryCapacity, prefix, ".aerbin") != 0)
        return -1;
    if (appendPathSuffix(metadataPath, metadataCapacity, prefix, ".json") != 0)
        return -1;
    return 0;
}

static int makeOutputPaths(void)
{
    const char *prefix = getenv("AER_DRIVEBOARD_OUTPUT");
    if (buildOutputPaths(prefix, g_recorder.startTimestampNs,
                         g_recorder.binaryPath, sizeof(g_recorder.binaryPath),
                         g_recorder.metadataPath, sizeof(g_recorder.metadataPath)) != 0)
    {
        g_recorder.binaryPath[0] = '\0';
        g_recorder.metadataPath[0] = '\0';
        fprintf(stderr, "AER drive-board recorder not started: output prefix is too long for capture paths\n");
        return -1;
    }
    snprintf(g_recorder.captureId, sizeof(g_recorder.captureId), "aer-%" PRIu64 "-%" PRIu64,
             (uint64_t)aer_getpid(), g_recorder.startTimestampNs);
    const char *marker = getenv("AER_DRIVEBOARD_MARKER_FILE");
    if (marker != NULL && marker[0] != '\0')
    {
        snprintf(g_recorder.markerPath, sizeof(g_recorder.markerPath), "%s", marker);
        FILE *markerFile = fopen(g_recorder.markerPath, "rb");
        if (markerFile != NULL)
        {
            if (fseek(markerFile, 0, SEEK_END) == 0)
                g_recorder.markerOffset = ftell(markerFile);
            fclose(markerFile);
        }
    }
    return 0;
}

void aerDriveboardRecorderInitialize(const AerDriveboardRecorderMetadata *metadata)
{
    if (g_recorder.initialized)
        return;
    memset(&g_recorder, 0, sizeof(g_recorder));
    g_recorder.initialized = 1;

    if (!environmentEnabled())
        return;
    if (metadata == NULL || !supportedRevision(metadata->gameRevision))
    {
        fprintf(stderr, "AER drive-board recorder not started: target revision is not DVP-0015/DVP-0015A\n");
        return;
    }

    g_recorder.metadata = *metadata;
    copyMetadataValue(g_recorder.gameRevision, sizeof(g_recorder.gameRevision), metadata->gameRevision);
    copyMetadataValue(g_recorder.executableHash, sizeof(g_recorder.executableHash), metadata->gameExecutableHash);
    copyMetadataValue(g_recorder.cabinetType, sizeof(g_recorder.cabinetType), metadata->cabinetType);
    copyMetadataValue(g_recorder.cabinetId, sizeof(g_recorder.cabinetId), metadata->cabinetId);
    g_recorder.metadata.gameRevision = g_recorder.gameRevision;
    g_recorder.metadata.gameExecutableHash = g_recorder.executableHash;
    g_recorder.metadata.cabinetType = g_recorder.cabinetType;
    g_recorder.metadata.cabinetId = g_recorder.cabinetId;
    g_recorder.startTimestampNs = aerDriveboardRecorderMonotonicNs();
    g_recorder.nextSequence = 1;
    atomic_init(&g_recorder.droppedRecords, 0);
    g_recorder.writtenRecords = 0;
    g_recorder.overflowReportedThrough = 0;
    atomic_init(&g_recorder.captureIncomplete, 0);
#ifdef AER_RECORDER_TESTING
    atomic_init(&g_recorder.writerPaused, 0);
#endif
    pthread_mutex_init(&g_recorder.mutex, NULL);
    pthread_cond_init(&g_recorder.condition, NULL);
    if (makeOutputPaths() != 0)
        return;

    g_recorder.binaryFile = fopen(g_recorder.binaryPath, "wb");
    if (g_recorder.binaryFile == NULL)
    {
        fprintf(stderr, "AER drive-board recorder failed to open %s: %s\n", g_recorder.binaryPath, strerror(errno));
        return;
    }

    AerBinaryHeader header;
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, "AERDBR1", 7);
    header.formatVersion = 1;
    header.headerSize = sizeof(header);
    header.endianMarker = UINT32_C(0x01020304);
    snprintf(header.schema, sizeof(header.schema), "%s", AER_DRIVEBOARD_SCHEMA);
    if (fwrite(&header, sizeof(header), 1, g_recorder.binaryFile) != 1)
    {
        fclose(g_recorder.binaryFile);
        g_recorder.binaryFile = NULL;
        fprintf(stderr, "AER drive-board recorder failed to write the binary header\n");
        return;
    }
    fflush(g_recorder.binaryFile);

    g_recorder.enabled = 1;
    writeMetadata();
    if (pthread_create(&g_recorder.writerThread, NULL, writerMain, NULL) != 0)
    {
        g_recorder.enabled = 0;
        fclose(g_recorder.binaryFile);
        g_recorder.binaryFile = NULL;
        fprintf(stderr, "AER drive-board recorder failed to start writer thread\n");
        return;
    }
    g_recorder.writerStarted = 1;
#if !defined(__linux__)
    atexit(aerDriveboardRecorderShutdown);
#endif
    fprintf(stderr, "AER drive-board recorder enabled: %s (research-only raw transport capture)\n", g_recorder.binaryPath);
}

void aerDriveboardRecorderShutdown(void)
{
    if (!g_recorder.initialized || !g_recorder.enabled)
        return;
    pthread_mutex_lock(&g_recorder.mutex);
    g_recorder.stopRequested = 1;
#ifdef AER_RECORDER_TESTING
    atomic_store(&g_recorder.writerPaused, 0);
#endif
    pthread_cond_signal(&g_recorder.condition);
    pthread_mutex_unlock(&g_recorder.mutex);
    if (g_recorder.writerStarted)
        pthread_join(g_recorder.writerThread, NULL);
    g_recorder.endTimestampNs = aerDriveboardRecorderMonotonicNs();
    if (g_recorder.binaryFile != NULL)
    {
        fflush(g_recorder.binaryFile);
        fclose(g_recorder.binaryFile);
        g_recorder.binaryFile = NULL;
    }
    writeMetadata();
    g_recorder.enabled = 0;
}

int aerDriveboardRecorderEnabled(void)
{
    return g_recorder.enabled;
}

static void captureEvent(uint16_t eventType, uint64_t timestampNs, AerDriveboardEndpoint endpoint, int fd,
                         const void *bytes, size_t requestedCount, ssize_t result, size_t availableCount)
{
    if (!g_recorder.enabled)
        return;
    uint16_t status = AER_CAPTURE_COMPLETE;
    size_t payloadLength = availableCount;
    if (payloadLength > AER_MAX_EVENT_BYTES)
    {
        payloadLength = AER_MAX_EVENT_BYTES;
        status = AER_CAPTURE_TRUNCATED;
        atomic_store(&g_recorder.captureIncomplete, 1);
    }
    if (payloadLength > 0 && bytes == NULL)
    {
        payloadLength = 0;
        status = AER_CAPTURE_INVALID_BUFFER;
        atomic_store(&g_recorder.captureIncomplete, 1);
    }
    AerQueuedEvent event;
    fillEvent(&event, eventType, status, timestampNs, endpoint, fd, requestedCount, result, bytes, payloadLength);
    enqueueEvent(&event);
}

void aerDriveboardRecorderCaptureWrite(uint64_t timestampNs, AerDriveboardEndpoint endpoint, int fd,
                                       const void *bytes, size_t requestedCount, ssize_t result)
{
    captureEvent(AER_DRIVEBOARD_EVENT_WRITE, timestampNs, endpoint, fd, bytes, requestedCount, result, requestedCount);
}

void aerDriveboardRecorderPrepareWrite(AerDriveboardPendingWrite *pending, AerDriveboardEndpoint endpoint,
                                       int fd, const void *bytes, size_t requestedCount)
{
    aerDriveboardRecorderPrepareWritePath(pending, AER_DRIVEBOARD_EVENT_WRITE, endpoint, fd, bytes, requestedCount);
}

void aerDriveboardRecorderPrepareWritePath(AerDriveboardPendingWrite *pending, AerDriveboardEventType eventType,
                                           AerDriveboardEndpoint endpoint, int fd, const void *bytes,
                                           size_t requestedCount)
{
    if (pending == NULL)
        return;
    memset(pending, 0, sizeof(*pending));
    if (!g_recorder.enabled)
        return;
    pending->active = 1;
    pending->eventType = eventType;
    pending->timestampNs = aerDriveboardRecorderMonotonicNs();
    pending->endpoint = endpoint;
    pending->fileDescriptor = fd;
    pending->requestedCount = requestedCount;
    pending->capturedCount = requestedCount;
    pending->captureStatus = AER_CAPTURE_COMPLETE;
    if (pending->capturedCount > sizeof(pending->bytes))
    {
        pending->capturedCount = sizeof(pending->bytes);
        pending->captureStatus = AER_CAPTURE_TRUNCATED;
        atomic_store(&g_recorder.captureIncomplete, 1);
    }
    if (pending->capturedCount > 0 && bytes == NULL)
    {
        pending->capturedCount = 0;
        pending->captureStatus = AER_CAPTURE_INVALID_BUFFER;
        atomic_store(&g_recorder.captureIncomplete, 1);
    }
    if (pending->capturedCount > 0)
        memcpy(pending->bytes, bytes, pending->capturedCount);
}

void aerDriveboardRecorderCompleteWrite(AerDriveboardPendingWrite *pending, ssize_t result)
{
    if (pending == NULL || !pending->active || !g_recorder.enabled)
        return;
    AerQueuedEvent event;
    fillEvent(&event, pending->eventType, pending->captureStatus, pending->timestampNs, pending->endpoint,
              pending->fileDescriptor, pending->requestedCount, result, pending->bytes, pending->capturedCount);
    enqueueEvent(&event);
    pending->active = 0;
}

void aerDriveboardRecorderCaptureRead(uint64_t timestampNs, AerDriveboardEndpoint endpoint, int fd,
                                      const void *bytes, size_t requestedCount, ssize_t result)
{
    size_t returnedCount = result > 0 ? (size_t)result : 0;
    captureEvent(AER_DRIVEBOARD_EVENT_READ, timestampNs, endpoint, fd, bytes, requestedCount, result, returnedCount);
}

void aerDriveboardRecorderCaptureDuplicate(uint64_t timestampNs, AerDriveboardEndpoint endpoint,
                                           int sourceFd, int destinationFd)
{
    captureEvent(AER_DRIVEBOARD_EVENT_DUP, timestampNs, endpoint, sourceFd, NULL, 0, destinationFd, 0);
}

void aerDriveboardRecorderMark(const char *marker)
{
    if (!g_recorder.enabled || marker == NULL)
        return;
    size_t length = strnlen(marker, AER_MAX_MARKER_BYTES);
    AerQueuedEvent event;
    fillEvent(&event, AER_DRIVEBOARD_EVENT_MARKER, AER_CAPTURE_COMPLETE, aerDriveboardRecorderMonotonicNs(),
              AER_DRIVEBOARD_ENDPOINT_UNKNOWN, -1, length, (ssize_t)length, marker, length);
    enqueueEvent(&event);
}

#ifdef AER_RECORDER_TESTING
void aerDriveboardRecorderTestPauseWriter(int paused)
{
    atomic_store(&g_recorder.writerPaused, paused != 0);
}

int aerDriveboardRecorderTestBuildOutputPaths(const char *prefix, uint64_t timestamp,
                                              char *binaryPath, size_t binaryCapacity,
                                              char *metadataPath, size_t metadataCapacity)
{
    return buildOutputPaths(prefix, timestamp, binaryPath, binaryCapacity, metadataPath, metadataCapacity);
}
#endif
