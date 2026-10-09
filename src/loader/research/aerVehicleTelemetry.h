#ifndef AER_VEHICLE_TELEMETRY_H
#define AER_VEHICLE_TELEMETRY_H

#include <stdint.h>

#define AER_VEHICLE_TELEMETRY_SCHEMA "AER_VEHICLE_FFB_V1"

#ifdef __cplusplus
extern "C" {
#endif

void aerVehicleTelemetryInitialize(const char *revision);
void aerVehicleTelemetryShutdown(void);
int aerVehicleTelemetryEnabled(void);
void aerVehicleTelemetryObserveDataSet(const void *car, const void *carWork);
void aerVehicleTelemetryObserveMoveSend(void);
void aerVehicleTelemetryObserveCommand(const unsigned char *logicalBytes, int logicalLength);

#ifdef AER_VEHICLE_TELEMETRY_TESTING
void aerVehicleTelemetryTestSetTimestamp(uint64_t timestamp);
#endif

#ifdef __cplusplus
}
#endif
#endif
