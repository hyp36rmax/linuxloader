#ifndef AER_VIRTUAL_DRIVEBOARD_BOOTSTRAP_H
#define AER_VIRTUAL_DRIVEBOARD_BOOTSTRAP_H

#include "aerVirtualDriveboard.h"

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    AER_VDB_BRIDGE_OPEN    = 1u << 0,
    AER_VDB_BRIDGE_READ    = 1u << 1,
    AER_VDB_BRIDGE_WRITE   = 1u << 2,
    AER_VDB_BRIDGE_FWRITE  = 1u << 3,
    AER_VDB_BRIDGE_SELECT  = 1u << 4,
    AER_VDB_BRIDGE_IOCTL   = 1u << 5,
    AER_VDB_BRIDGE_CLOSE   = 1u << 6,
    AER_VDB_BRIDGE_WRITEV  = 1u << 7,
    AER_VDB_BRIDGE_DUP     = 1u << 8
};

#define AER_VDB_REQUIRED_BRIDGE_CAPABILITIES \
    (AER_VDB_BRIDGE_OPEN | AER_VDB_BRIDGE_READ | AER_VDB_BRIDGE_WRITE | \
     AER_VDB_BRIDGE_FWRITE | AER_VDB_BRIDGE_SELECT | AER_VDB_BRIDGE_IOCTL | \
     AER_VDB_BRIDGE_CLOSE)

typedef enum AerVdbBootstrapResult
{
    AER_VDB_BOOTSTRAP_ELIGIBLE,
    AER_VDB_BOOTSTRAP_DISABLED,
    AER_VDB_BOOTSTRAP_IDENTITY_REJECTED,
    AER_VDB_BOOTSTRAP_CONFIG_REJECTED,
    AER_VDB_BOOTSTRAP_BRIDGE_REJECTED,
    AER_VDB_BOOTSTRAP_ALREADY_EVALUATED
} AerVdbBootstrapResult;

typedef struct AerVdbBootstrapInput
{
    int requested;
    const char *revision;
    uint32_t crc32;
    const char *executablePath;
    const char *testExpectedSha256;
    int skipCabinetCheck;
    int emulateDriveboard;
    int physicalSerialRequested;
    int boardCount;
    uint32_t bridgeCapabilities;
} AerVdbBootstrapInput;

typedef struct AerVdbBootstrap
{
    int evaluated;
    int eligible;
    int mutationCount;
    AerVdbVerifyResult verification;
    AerVdbConfigResult configuration;
    AerVdbBootstrapResult result;
} AerVdbBootstrap;

typedef struct AerVdbOriginalSite
{
    uint32_t address;
    uint8_t size;
    uint8_t expected[8];
    const char *owner;
    const char *existingPatch;
} AerVdbOriginalSite;

const AerVdbOriginalSite *aerVdbOriginalManifest(size_t *count);
void aerVdbBootstrapReset(AerVdbBootstrap *bootstrap);
AerVdbBootstrapResult aerVdbBootstrapEvaluate(AerVdbBootstrap *bootstrap,
                                               const AerVdbBootstrapInput *input);

#ifdef __cplusplus
}
#endif
#endif
