#define _POSIX_C_SOURCE 200809L

#include "../src/loader/research/aerDriveboardRecorder.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#pragma pack(push, 1)
typedef struct
{
    char magic[8];
    uint16_t formatVersion;
    uint16_t headerSize;
    uint32_t endianMarker;
    char schema[32];
} TestBinaryHeader;

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
} TestRecordHeader;
#pragma pack(pop)

static int fileExists(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0;
}

static void removeOutputs(const char *prefix)
{
    char path[512];
    snprintf(path, sizeof(path), "%s.aerbin", prefix);
    unlink(path);
    snprintf(path, sizeof(path), "%s.json", prefix);
    unlink(path);
}

static void testDisabled(const char *prefix)
{
    unsetenv("AER_DRIVEBOARD_RECORDER");
    setenv("AER_DRIVEBOARD_OUTPUT", prefix, 1);
    removeOutputs(prefix);
    AerDriveboardRecorderMetadata metadata = {"DVP-0015", "test-hash", "SDX", "1", 1, 1};
    aerDriveboardRecorderInitialize(&metadata);
    aerDriveboardRecorderInitialize(&metadata);
    assert(!aerDriveboardRecorderEnabled());
    const uint8_t bytes[] = {1, 2, 3};
    aerDriveboardRecorderCaptureWrite(1, AER_DRIVEBOARD_ENDPOINT_SERIAL0, 4, bytes, sizeof(bytes), 3);
    char path[512];
    snprintf(path, sizeof(path), "%s.aerbin", prefix);
    assert(!fileExists(path));
    aerDriveboardRecorderShutdown();
    aerDriveboardRecorderShutdown();
}

static void testOutputPaths(void)
{
    char binary[1024];
    char metadata[1024];
    assert(aerDriveboardRecorderTestBuildOutputPaths("short/capture", 42, binary, sizeof(binary),
                                                      metadata, sizeof(metadata)) == 0);
    assert(strcmp(binary, "short/capture.aerbin") == 0);
    assert(strcmp(metadata, "short/capture.json") == 0);

    char maximumPrefix[1017];
    memset(maximumPrefix, 'x', sizeof(maximumPrefix) - 1);
    maximumPrefix[sizeof(maximumPrefix) - 1] = '\0';
    assert(aerDriveboardRecorderTestBuildOutputPaths(maximumPrefix, 42, binary, sizeof(binary),
                                                      metadata, sizeof(metadata)) == 0);
    assert(strlen(binary) == sizeof(binary) - 1);

    char excessivePrefix[1018];
    memset(excessivePrefix, 'y', sizeof(excessivePrefix) - 1);
    excessivePrefix[sizeof(excessivePrefix) - 1] = '\0';
    assert(aerDriveboardRecorderTestBuildOutputPaths(excessivePrefix, 42, binary, sizeof(binary),
                                                      metadata, sizeof(metadata)) != 0);

    assert(aerDriveboardRecorderTestBuildOutputPaths("", 42, binary, sizeof(binary),
                                                      metadata, sizeof(metadata)) == 0);
    assert(strcmp(binary, "aer_driveboard_42.aerbin") == 0);
    assert(strcmp(metadata, "aer_driveboard_42.json") == 0);

    assert(aerDriveboardRecorderTestBuildOutputPaths("abcd", 42, binary, 11,
                                                      metadata, sizeof(metadata)) != 0);
    assert(aerDriveboardRecorderTestBuildOutputPaths("abcd", 42, binary, sizeof(binary),
                                                      metadata, 9) != 0);
}

static void verifyMetadata(const char *prefix)
{
    char path[512];
    snprintf(path, sizeof(path), "%s.json", prefix);
    FILE *file = fopen(path, "rb");
    assert(file != NULL);
    char text[8192];
    size_t length = fread(text, 1, sizeof(text) - 1, file);
    text[length] = '\0';
    fclose(file);
    assert(strstr(text, "\"schema\": \"AER_DRIVEBOARD_RAW_V1\"") != NULL);
    assert(strstr(text, "\"game_revision\": \"DVP-0015\"") != NULL);
    assert(strstr(text, "\"executable_sha256\": \"synthetic-sha256\"") != NULL);
    assert(strstr(text, "\"inbound_provenance\": \"LOADER-SYNTHESIZED RESPONSES\"") != NULL);
    assert(strstr(text, "\"dropped_record_count\": 0") == NULL);
    assert(strstr(text, "\"capture_complete\": false") != NULL);
}

static void verifyBinary(const char *prefix)
{
    char path[512];
    snprintf(path, sizeof(path), "%s.aerbin", prefix);
    FILE *file = fopen(path, "rb");
    assert(file != NULL);
    TestBinaryHeader binaryHeader;
    assert(fread(&binaryHeader, sizeof(binaryHeader), 1, file) == 1);
    assert(memcmp(binaryHeader.magic, "AERDBR1", 7) == 0);
    assert(binaryHeader.formatVersion == 1);
    assert(strcmp(binaryHeader.schema, AER_DRIVEBOARD_SCHEMA) == 0);

    uint64_t previousSequence = 0;
    int sawPartial = 0;
    int sawCombined = 0;
    int sawZeroLength = 0;
    int sawFailed = 0;
    int sawRead = 0;
    int sawMarker = 0;
    int sawFileMarker = 0;
    int sawOverflow = 0;
    int sawWritev = 0;
    int sawFwrite = 0;
    int sawDuplicate = 0;
    int sawSerial1 = 0;
    TestRecordHeader record;
    uint8_t payload[4096];
    while (fread(&record, sizeof(record), 1, file) == 1)
    {
        assert(record.recordSize == sizeof(record) + record.payloadLength);
        assert(record.payloadLength <= sizeof(payload));
        if (record.payloadLength > 0)
            assert(fread(payload, record.payloadLength, 1, file) == 1);
        assert(record.sequence > previousSequence);
        assert(record.timestampNs > 0);
        previousSequence = record.sequence;
        if (record.eventType == 1 && record.requestedCount == 2 && record.operationResult == 2)
        {
            const uint8_t expected[] = {0x11, 0x22};
            assert(record.payloadLength == sizeof(expected));
            assert(memcmp(payload, expected, sizeof(expected)) == 0);
            sawPartial = 1;
        }
        if (record.eventType == 1 && record.requestedCount == 8)
        {
            const uint8_t expected[] = {1, 2, 3, 4, 5, 6, 7, 8};
            assert(record.payloadLength == sizeof(expected));
            assert(memcmp(payload, expected, sizeof(expected)) == 0);
            sawCombined = 1;
        }
        if (record.eventType == 1 && record.requestedCount == 0)
            sawZeroLength = 1;
        if (record.eventType == 1 && record.operationResult == -1)
            sawFailed = 1;
        if (record.eventType == 2)
        {
            const uint8_t expected[] = {0x44};
            assert(record.requestedCount == 16);
            assert(record.operationResult == 1);
            assert(record.payloadLength == 1);
            assert(memcmp(payload, expected, 1) == 0);
            sawRead = 1;
        }
        if (record.eventType == 3)
        {
            sawMarker = 1;
            if (record.payloadLength == 4 && memcmp(payload, "MENU", 4) == 0)
                sawFileMarker = 1;
        }
        if (record.eventType == 4 && record.operationResult > 0)
            sawOverflow = 1;
        if (record.eventType == AER_DRIVEBOARD_EVENT_WRITEV && record.requestedCount == 3 &&
            record.operationResult == 2 && record.endpoint == AER_DRIVEBOARD_ENDPOINT_SERIAL0)
        {
            const uint8_t expected[] = {0xa1, 0xa2, 0xa3};
            assert(record.payloadLength == sizeof(expected));
            assert(memcmp(payload, expected, sizeof(expected)) == 0);
            sawWritev = 1;
        }
        if (record.eventType == AER_DRIVEBOARD_EVENT_FWRITE && record.requestedCount == 4 &&
            record.operationResult == 1 && record.endpoint == AER_DRIVEBOARD_ENDPOINT_SERIAL1)
        {
            const uint8_t expected[] = {0xb1, 0xb2, 0xb3, 0xb4};
            assert(record.payloadLength == sizeof(expected));
            assert(memcmp(payload, expected, sizeof(expected)) == 0);
            sawFwrite = 1;
            sawSerial1 = 1;
        }
        if (record.eventType == AER_DRIVEBOARD_EVENT_DUP && record.fileDescriptor == 7 &&
            record.operationResult == 11 && record.endpoint == AER_DRIVEBOARD_ENDPOINT_SERIAL0)
            sawDuplicate = 1;
    }
    fclose(file);
    if (!(sawPartial && sawCombined && sawZeroLength && sawFailed && sawRead && sawMarker && sawFileMarker &&
          sawOverflow && sawWritev && sawFwrite && sawDuplicate && sawSerial1))
    {
        fprintf(stderr, "missing record: partial=%d combined=%d zero=%d failed=%d read=%d marker=%d file_marker=%d overflow=%d writev=%d fwrite=%d dup=%d serial1=%d\n",
                sawPartial, sawCombined, sawZeroLength, sawFailed, sawRead, sawMarker, sawFileMarker, sawOverflow,
                sawWritev, sawFwrite, sawDuplicate, sawSerial1);
        abort();
    }
}

static void testCapture(const char *prefix)
{
    setenv("AER_DRIVEBOARD_RECORDER", "1", 1);
    setenv("AER_DRIVEBOARD_OUTPUT", prefix, 1);
    char markerPath[512];
    snprintf(markerPath, sizeof(markerPath), "%s.markers", prefix);
    FILE *markerFile = fopen(markerPath, "wb");
    assert(markerFile != NULL);
    fputs("OLD\n", markerFile);
    fclose(markerFile);
    setenv("AER_DRIVEBOARD_MARKER_FILE", markerPath, 1);
    removeOutputs(prefix);
    AerDriveboardRecorderMetadata metadata = {"DVP-0015", "synthetic-sha256", "SDX", "1", 1, 1};
    aerDriveboardRecorderInitialize(&metadata);
    aerDriveboardRecorderInitialize(&metadata);
    assert(aerDriveboardRecorderEnabled());

    uint64_t timestamp = aerDriveboardRecorderMonotonicNs();
    uint8_t partial[] = {0x11, 0x22};
    const uint8_t combined[] = {1, 2, 3, 4, 5, 6, 7, 8};
    const uint8_t inbound[] = {0x44};
    AerDriveboardPendingWrite pending;
    aerDriveboardRecorderPrepareWrite(&pending, AER_DRIVEBOARD_ENDPOINT_SERIAL0, 7, partial, sizeof(partial));
    partial[0] = 0x99;
    aerDriveboardRecorderCompleteWrite(&pending, 2);
    timestamp = aerDriveboardRecorderMonotonicNs();
    aerDriveboardRecorderCaptureWrite(timestamp++, AER_DRIVEBOARD_ENDPOINT_SERIAL0, 7, combined, sizeof(combined), 8);
    aerDriveboardRecorderCaptureWrite(timestamp++, AER_DRIVEBOARD_ENDPOINT_SERIAL0, 7, NULL, 0, 0);
    aerDriveboardRecorderCaptureWrite(timestamp++, AER_DRIVEBOARD_ENDPOINT_SERIAL0, 7, partial, sizeof(partial), -1);
    aerDriveboardRecorderCaptureRead(timestamp++, AER_DRIVEBOARD_ENDPOINT_SERIAL0, 7, inbound, 16, 1);
    const uint8_t writevBytes[] = {0xa1, 0xa2, 0xa3};
    aerDriveboardRecorderPrepareWritePath(&pending, AER_DRIVEBOARD_EVENT_WRITEV,
                                          AER_DRIVEBOARD_ENDPOINT_SERIAL0, 7,
                                          writevBytes, sizeof(writevBytes));
    aerDriveboardRecorderCompleteWrite(&pending, 2);
    const uint8_t fwriteBytes[] = {0xb1, 0xb2, 0xb3, 0xb4};
    aerDriveboardRecorderPrepareWritePath(&pending, AER_DRIVEBOARD_EVENT_FWRITE,
                                          AER_DRIVEBOARD_ENDPOINT_SERIAL1, 9,
                                          fwriteBytes, sizeof(fwriteBytes));
    aerDriveboardRecorderCompleteWrite(&pending, 1);
    aerDriveboardRecorderCaptureDuplicate(timestamp++, AER_DRIVEBOARD_ENDPOINT_SERIAL0, 7, 11);
    aerDriveboardRecorderMark("STATIONARY");
    markerFile = fopen(markerPath, "ab");
    assert(markerFile != NULL);
    fputs("MENU\n", markerFile);
    fclose(markerFile);

    aerDriveboardRecorderTestPauseWriter(1);
    struct timespec pause = {0, 20000000};
    nanosleep(&pause, NULL);
    timestamp = aerDriveboardRecorderMonotonicNs();
    for (int i = 0; i < 400; ++i)
    {
        uint8_t byte = (uint8_t)i;
        aerDriveboardRecorderCaptureWrite(timestamp++, AER_DRIVEBOARD_ENDPOINT_SERIAL0, 7, &byte, 1, 1);
    }
    aerDriveboardRecorderTestPauseWriter(0);
    aerDriveboardRecorderShutdown();
    aerDriveboardRecorderShutdown();
    verifyBinary(prefix);
    verifyMetadata(prefix);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    if (strcmp(argv[1], "disabled") == 0)
        testDisabled(argv[2]);
    else if (strcmp(argv[1], "capture") == 0)
        testCapture(argv[2]);
    else if (strcmp(argv[1], "paths") == 0)
        testOutputPaths();
    else
        return 2;
    return 0;
}
