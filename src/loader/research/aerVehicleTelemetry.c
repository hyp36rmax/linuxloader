#include "aerVehicleTelemetry.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aerDriveboardRecorder.h"

enum { VALID_CAR_WORK=1u<<0, VALID_FRONT_DIRECTION=1u<<1, VALID_ROAD_STATE=1u<<2, VALID_COMMAND=1u<<3 };
typedef struct VehicleSnapshot {
    uint64_t sequence, timestamp;
    int16_t frontDirection;
    uint32_t roadAggregate, frontLeftRoad, frontRightRoad;
    float thresholdState;
    uint32_t valid;
} VehicleSnapshot;
typedef struct VehicleTelemetryState {
    int initialized, enabled;
    uint64_t sequence;
    FILE *file;
    VehicleSnapshot last;
    char path[1024];
#ifdef AER_VEHICLE_TELEMETRY_TESTING
    uint64_t testTimestamp;
#endif
} VehicleTelemetryState;
static VehicleTelemetryState g_vehicle;

static int enabledValue(const char *value){return value&&(!strcmp(value,"1")||!strcmp(value,"true")||!strcmp(value,"TRUE"));}
static uint64_t now(void){
#ifdef AER_VEHICLE_TELEMETRY_TESTING
    return g_vehicle.testTimestamp;
#else
    return aerDriveboardRecorderMonotonicNs();
#endif
}
static uint32_t readU32(const unsigned char *p,size_t offset){uint32_t v;memcpy(&v,p+offset,sizeof(v));return v;}
static int16_t readI16(const unsigned char *p,size_t offset){int16_t v;memcpy(&v,p+offset,sizeof(v));return v;}
static float readF32(const unsigned char *p,size_t offset){float v;memcpy(&v,p+offset,sizeof(v));return v;}
static void writeRow(const char *event,const VehicleSnapshot *s,int channel,int command,int a,int b,int checksumKnown)
{
    if(!g_vehicle.file)return;
    fprintf(g_vehicle.file,"%" PRIu64 ",%" PRIu64 ",%s,%u,%d,%u,%u,%.9g,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
            ++g_vehicle.sequence,now(),event,s?s->valid:0,s?s->frontDirection:0,
            s?s->roadAggregate:0,s?s->frontLeftRoad:0,s?s->thresholdState:0.0,
            s?(int)s->frontRightRoad:0,-1,-1,-1,-1,channel,command,a,b,checksumKnown);
}
void aerVehicleTelemetryInitialize(const char *revision)
{
    if(g_vehicle.initialized)return;
    memset(&g_vehicle,0,sizeof(g_vehicle));
    g_vehicle.initialized=1;
    if(!enabledValue(getenv("AER_VEHICLE_TELEMETRY"))||!revision||strcmp(revision,"DVP-0015A"))return;
    const char *path=getenv("AER_VEHICLE_TELEMETRY_OUTPUT");snprintf(g_vehicle.path,sizeof(g_vehicle.path),"%s",path&&path[0]?path:"aer_vehicle_ffb_v1.csv");
    g_vehicle.file=fopen(g_vehicle.path,"wb");if(!g_vehicle.file)return;
    fprintf(g_vehicle.file,"#schema=%s\nsequence,timestamp_ns,event,validity_flags,front_tire_direction_s16,road_aggregate_mask,front_left_road_mask,threshold_state_f32,front_right_road_mask,vehicle_id,speed,steering_input,vehicle_orientation,logical_channel,command,value_a,value_b,checksum_known\n",AER_VEHICLE_TELEMETRY_SCHEMA);
    g_vehicle.enabled=1;
#if !defined(__linux__)
    atexit(aerVehicleTelemetryShutdown);
#endif
}
void aerVehicleTelemetryShutdown(void){if(g_vehicle.file){fflush(g_vehicle.file);fclose(g_vehicle.file);g_vehicle.file=NULL;}g_vehicle.enabled=0;}
int aerVehicleTelemetryEnabled(void){return g_vehicle.enabled;}
void aerVehicleTelemetryObserveDataSet(const void *car,const void *carWork)
{
    (void)car;
    if(!g_vehicle.enabled)return;
    VehicleSnapshot s;
    memset(&s,0,sizeof(s));
    s.sequence=g_vehicle.sequence+1;
    s.timestamp=now();
    if(carWork){const unsigned char *p=carWork;s.valid=VALID_CAR_WORK|VALID_FRONT_DIRECTION|VALID_ROAD_STATE;s.frontDirection=readI16(p,0x054);s.thresholdState=readF32(p,0x3ac);s.roadAggregate=readU32(p,0x3fc);s.frontLeftRoad=readU32(p,0x404);s.frontRightRoad=readU32(p,0x408);}g_vehicle.last=s;writeRow("data_set",&s,-1,-1,-1,-1,0);
}
void aerVehicleTelemetryObserveMoveSend(void){if(g_vehicle.enabled)writeRow("move_send",&g_vehicle.last,-1,-1,-1,-1,0);}
void aerVehicleTelemetryObserveCommand(const unsigned char *bytes,int length)
{
    if(!g_vehicle.enabled||!bytes||length<3)return;
    int channels=length>=6?2:1;
    for(int channel=0;channel<channels;channel++){const unsigned char *p=bytes+channel*3;VehicleSnapshot s=g_vehicle.last;s.valid|=VALID_COMMAND;writeRow("send_out",&s,channel,p[0]&0x7f,p[1],p[2],0);}
}
#ifdef AER_VEHICLE_TELEMETRY_TESTING
void aerVehicleTelemetryTestSetTimestamp(uint64_t timestamp){g_vehicle.testTimestamp=timestamp;}
#endif
