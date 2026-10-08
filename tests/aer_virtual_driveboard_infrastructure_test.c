#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "loader/research/aerVirtualDriveboard.h"

static void make_frame(uint8_t *p, uint8_t command, uint8_t a, uint8_t b)
{
    p[0]=(uint8_t)(command|0x80);p[1]=a;p[2]=b;p[3]=(uint8_t)(command^a^b);
}

static void test_verifier(const char *path)
{
    static const char abcHash[]="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    uint8_t expected[]={0xaa,0xbb}, actual[]={0xaa,0xbb}, wrong[]={0xaa,0xbc};
    AerVdbManifestEntry good={0x08103eaa,expected,actual,2,1};
    AerVdbTarget t={1,AER_VDB_EXPECTED_REVISION,AER_VDB_EXPECTED_CRC32,path,abcHash,&good,1};
    char hash[65];
    assert(aerVdbSha256File(path,hash)&&!strcmp(hash,abcHash));
    assert(aerVdbVerifyTarget(&t)==AER_VDB_VERIFY_OK);
    t.requested=0;assert(aerVdbVerifyTarget(&t)==AER_VDB_VERIFY_DISABLED);t.requested=1;
    t.revision="DVP-0015";assert(aerVdbVerifyTarget(&t)==AER_VDB_VERIFY_WRONG_REVISION);t.revision=AER_VDB_EXPECTED_REVISION;
    t.crc32=0;assert(aerVdbVerifyTarget(&t)==AER_VDB_VERIFY_WRONG_CRC);t.crc32=AER_VDB_EXPECTED_CRC32;
    t.executablePath="/missing/aer-jennifer";assert(aerVdbVerifyTarget(&t)==AER_VDB_VERIFY_MISSING_EXECUTABLE);t.executablePath=path;
    t.testExpectedSha256="00";assert(aerVdbVerifyTarget(&t)==AER_VDB_VERIFY_WRONG_SHA256);t.testExpectedSha256=abcHash;
    t.manifest=NULL;assert(aerVdbVerifyTarget(&t)==AER_VDB_VERIFY_INCOMPLETE_MANIFEST);t.manifest=&good;
    good.complete=0;assert(aerVdbVerifyTarget(&t)==AER_VDB_VERIFY_INCOMPLETE_MANIFEST);good.complete=1;
    good.actual=wrong;assert(aerVdbVerifyTarget(&t)==AER_VDB_VERIFY_BYTE_MISMATCH);
}

static void test_config(void)
{
    AerVdbConfig c={1,0,0,0,1,AER_VDB_VERIFY_OK};
    assert(aerVdbValidateConfig(&c)==AER_VDB_CONFIG_OK);
    c.requested=0;assert(aerVdbValidateConfig(&c)==AER_VDB_CONFIG_DISABLED);c.requested=1;
    c.skipCabinetCheck=1;assert(aerVdbValidateConfig(&c)==AER_VDB_CONFIG_SKIP_CONFLICT);c.skipCabinetCheck=0;
    c.emulateDriveboard=1;assert(aerVdbValidateConfig(&c)==AER_VDB_CONFIG_EMULATOR_CONFLICT);c.emulateDriveboard=0;
    c.physicalSerialRequested=1;assert(aerVdbValidateConfig(&c)==AER_VDB_CONFIG_PHYSICAL_CONFLICT);c.physicalSerialRequested=0;
    c.boardCount=0;assert(aerVdbValidateConfig(&c)==AER_VDB_CONFIG_INVALID_BOARD_COUNT);c.boardCount=3;assert(aerVdbValidateConfig(&c)==AER_VDB_CONFIG_INVALID_BOARD_COUNT);c.boardCount=1;
    c.verification=AER_VDB_VERIFY_WRONG_SHA256;assert(aerVdbValidateConfig(&c)==AER_VDB_CONFIG_UNSUPPORTED_TARGET);
    c.verification=AER_VDB_VERIFY_INCOMPLETE_MANIFEST;assert(aerVdbValidateConfig(&c)==AER_VDB_CONFIG_INCOMPLETE_VERIFICATION);
}

static void test_transport_io(void)
{
    AerVdbTransport t;uint8_t f[4],bad[4],out[2];const void *parts[2];size_t sizes[2];
    aerVdbTransportInit(&t,1);assert(t.lifecycle==AER_VDB_IDLE);assert(aerVdbTransportStart(&t));
    make_frame(f,0x7f,0,0);parts[0]=f;parts[1]=f+2;sizes[0]=2;sizes[1]=2;
    assert(aerVdbTransportWritev(&t,parts,sizes,2)==4&&t.requestCount==1);
    assert(aerVdbTransportReadable(&t)==0);errno=0;assert(aerVdbTransportRead(&t,out,1)==-1&&errno==EAGAIN);
    assert(aerVdbTransportQueueAssumedResponse(&t,0x11));assert(aerVdbTransportQueueAssumedResponse(&t,0x22));
    assert(aerVdbTransportReadable(&t)==2);assert(aerVdbTransportRead(&t,out,1)==1&&out[0]==0x11);assert(aerVdbTransportReadable(&t)==1);assert(aerVdbTransportRead(&t,out,2)==1&&out[0]==0x22);
    assert(aerVdbTransportWrite(&t,f,2)==2);assert(aerVdbTransportWrite(&t,f+2,2)==2&&t.requestCount==2);
    assert(aerVdbTransportFwrite(&t,f,2,2)==2&&t.requestCount==3);
    memcpy(bad,f,4);bad[3]^=1;assert(aerVdbTransportWrite(&t,bad,4)==-1&&t.lifecycle==AER_VDB_FAULT&&t.responseCount==0);
}

static void test_queue_timeout_disconnect(void)
{
    AerVdbTransport t;uint8_t f[4];int i;make_frame(f,0x7f,0,0);
    aerVdbTransportInit(&t,1);aerVdbTransportStart(&t);for(i=0;i<AER_VDB_QUEUE_CAPACITY;i++)assert(aerVdbTransportWrite(&t,f,4)==4);assert(aerVdbTransportWrite(&t,f,4)==-1&&t.lifecycle==AER_VDB_FAULT);
    aerVdbTransportInit(&t,1);aerVdbTransportStart(&t);for(i=0;i<900;i++)assert(aerVdbTransportTick(&t));assert(!aerVdbTransportTick(&t)&&t.lifecycle==AER_VDB_FAULT);
    aerVdbTransportInit(&t,1);aerVdbTransportStart(&t);aerVdbTransportDisconnect(&t);assert(t.lifecycle==AER_VDB_FAULT);aerVdbTransportShutdown(&t);aerVdbTransportShutdown(&t);assert(t.lifecycle==AER_VDB_SHUTDOWN);
}

static void test_sensor(void)
{
    AerVdbTransport t;int i;aerVdbTransportInit(&t,1);t.lifecycle=AER_VDB_CALIBRATING;
    assert(aerVdbSensorRequest(&t,1));assert(aerVdbSensorTick(&t));assert(t.sensorPosition==10);
    for(i=0;i<20;i++){assert(aerVdbSensorRequest(&t,1));assert(aerVdbSensorTick(&t));}assert(t.sensorPosition==96);
    assert(aerVdbSensorRequest(&t,-1));assert(aerVdbSensorTick(&t));assert(t.sensorPosition==86);
    assert(!t.physicalOutputAccessed);
    t.calibrationTicks=900;assert(!aerVdbSensorTick(&t)&&t.lifecycle==AER_VDB_FAULT);
}

static void test_registry(void)
{
    AerVdbTransport t;AerVdbDescriptorRegistry r;unsigned generation;int i;aerVdbTransportInit(&t,1);aerVdbRegistryInit(&r,&t);
    assert(aerVdbRegistryOpen(&r,10));assert(!aerVdbRegistryOpen(&r,10));assert(aerVdbRegistryDup(&r,10,11));assert(aerVdbRegistryLookup(&r,11)>=0);
    assert(!aerVdbRegistryDup(&r,99,12));
    generation=r.slots[aerVdbRegistryLookup(&r,10)].generation;
    assert(aerVdbRegistryClose(&r,10)&&t.lifecycle==AER_VDB_IDLE);
    assert(aerVdbRegistryOpen(&r,10));assert(r.slots[aerVdbRegistryLookup(&r,10)].generation>generation);
    for(i=12;i<18;i++)assert(aerVdbRegistryOpen(&r,i));
    assert(!aerVdbRegistryOpen(&r,18));
    assert(aerVdbRegistryClose(&r,10));for(i=12;i<18;i++)assert(aerVdbRegistryClose(&r,i));
    assert(aerVdbRegistryClose(&r,11)&&t.lifecycle==AER_VDB_SHUTDOWN);assert(!aerVdbRegistryClose(&r,11));
}

int main(int argc,char **argv)
{
    FILE *f;if(argc!=2)return 2;f=fopen(argv[1],"wb");assert(f);assert(fwrite("abc",1,3,f)==3);assert(!fclose(f));
    test_verifier(argv[1]);test_config();test_transport_io();test_queue_timeout_disconnect();test_sensor();test_registry();
    puts("AER-02H.1 virtual drive-board infrastructure: all tests passed");return 0;
}
