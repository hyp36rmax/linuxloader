#ifndef AER_ACTIVATION_DIAGNOSTICS_H
#define AER_ACTIVATION_DIAGNOSTICS_H

#include <stddef.h>
#include <stdint.h>

#define AER_ACTIVATION_DIAGNOSTIC_VERSION "AER_DRIVEBOARD_ACTIVATION_V1"

#ifdef __cplusplus
extern "C" {
#endif

void aerActivationDiagnosticsInitialize(const char *gameRevision);
void aerActivationDiagnosticsShutdown(void);
int aerActivationDiagnosticsEnabled(void);
int aerActivationDiagnosticsUsesAtexit(void);
void aerActivationDiagnosticsPatch(size_t address, const char *replacement, const char *label);
void aerActivationDiagnosticsCreateReturnOneHook(size_t address, const char *label);
void aerActivationDiagnosticsHooksEnabled(int result);
int aerActivationDiagnosticsSteeringHook(void);
int aerActivationDiagnosticsActuatorHook(void);
void aerActivationDiagnosticsSelectReadable(void);
void aerActivationDiagnosticsIoctl(int readable, uint8_t response);
void aerActivationDiagnosticsRead(int enableReadBefore, int wroteResponse, uint8_t response);
void aerActivationDiagnosticsResponseTransition(uint8_t before, uint8_t after);
void aerActivationDiagnosticsFirstWrite(uint64_t timestampNs, int endpoint, int writeApi,
                                        size_t requestedLength, int initialized,
                                        int enableRead, uint8_t response);
void aerActivationDiagnosticsFirstWriteResult(int64_t result);

#ifdef AER_ACTIVATION_DIAGNOSTICS_TESTING
void aerActivationDiagnosticsTestHookResult(size_t address, const char *label, int result);
#endif

#ifdef __cplusplus
}
#endif

#endif
