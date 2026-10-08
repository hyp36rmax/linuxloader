#ifndef AER_NATIVE_ACTIVATION_H
#define AER_NATIVE_ACTIVATION_H

#include <stddef.h>

#define AER_NATIVE_ACTIVATION_VERSION "AER_NATIVE_ACTIVATION_V1"

#ifdef __cplusplus
extern "C" {
#endif

void aerNativeActivationInitialize(const char *gameRevision, const char *executableHash,
                                   const char *loaderCommit);
void aerNativeActivationInstallHooks(int steeringReplacementActive);
void aerNativeActivationShutdown(void);
int aerNativeActivationEnabled(void);
int aerNativeActivationSteeringReplacement(void);

/* Called by the already-owned CabinetCtrl_InitDriver replacement hook. */
void aerNativeActivationObserveInitReplacement(int returnValue);

#ifdef AER_NATIVE_ACTIVATION_TESTING
void aerNativeActivationTestObserveState(int driverState, int checkState);
void aerNativeActivationTestCountPipeline(unsigned index);
void aerNativeActivationTestObserveSevenByte(int fromCabinetOff);
#endif

#ifdef __cplusplus
}
#endif

#endif
