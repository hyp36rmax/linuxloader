#include "../src/loader/research/aerVehicleTelemetry.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint64_t aerDriveboardRecorderMonotonicNs(void){return 0;}
static void put16(unsigned char *p,size_t o,int16_t v){memcpy(p+o,&v,sizeof(v));}
static void put32(unsigned char *p,size_t o,uint32_t v){memcpy(p+o,&v,sizeof(v));}
static void putf(unsigned char *p,size_t o,float v){memcpy(p+o,&v,sizeof(v));}
int main(int argc,char **argv)
{
    assert(argc==2);setenv("AER_VEHICLE_TELEMETRY","1",1);setenv("AER_VEHICLE_TELEMETRY_OUTPUT",argv[1],1);
    aerVehicleTelemetryInitialize("DVP-0015A");assert(aerVehicleTelemetryEnabled());
    unsigned char evwork[0x500]={0}, carWork[0x500]={0};
    put16(evwork,0x54,-321);putf(evwork,0x3ac,42.5f);
    put32(evwork,0x3fc,0x20);put32(evwork,0x404,0x10);put32(evwork,0x408,0x40);
    /* Distinct values catch accidental reads from the unrelated CAR_WORK pointer. */
    put16(carWork,0x54,1234);putf(carWork,0x3ac,-3.5f);
    put32(carWork,0x3fc,0xdeadbeef);put32(carWork,0x404,0x12345678);put32(carWork,0x408,0xffffffff);
    aerVehicleTelemetryTestSetTimestamp(100);aerVehicleTelemetryObserveDataSet(evwork,carWork);
    aerVehicleTelemetryTestSetTimestamp(110);aerVehicleTelemetryObserveMoveSend();
    unsigned char request[6]={0x0b,0x10,4,0x7d,0,0};aerVehicleTelemetryTestSetTimestamp(120);aerVehicleTelemetryObserveCommand(request,6);
    aerVehicleTelemetryShutdown();
    FILE *f=fopen(argv[1],"rb");assert(f);char text[4096];size_t n=fread(text,1,sizeof(text)-1,f);fclose(f);text[n]=0;
    assert(strstr(text,"#schema=AER_VEHICLE_FFB_V2"));assert(strstr(text,"data_set,7,-321,32,16,42.5,64"));assert(strstr(text,"send_out,15,-321,32,16,42.5,64,-1,-1,-1,-1,0,11,16,4,0"));
    puts("AER vehicle telemetry tests passed");return 0;
}
