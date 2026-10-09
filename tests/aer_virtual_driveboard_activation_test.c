#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include "loader/research/aerVirtualDriveboard.h"

typedef struct SensorEvidence { int writes; int board; int position; } SensorEvidence;

static void sensor_writer(int board,int position,void *context)
{
    SensorEvidence *evidence=context;
    evidence->writes++;
    evidence->board=board;
    evidence->position=position;
}
static void frame(uint8_t bytes[4],uint8_t command,uint8_t a,uint8_t b)
{
    bytes[0]=(uint8_t)(command|0x80);bytes[1]=a;bytes[2]=b;
    bytes[3]=(uint8_t)(command^a^b);
}

static uint8_t transact(AerVdbTransport *transport,uint8_t command,uint8_t a,uint8_t b)
{
    uint8_t request[4],response=0xff;
    frame(request,command,a,b);
    assert(aerVdbTransportWrite(transport,request,sizeof(request))==(ssize_t)sizeof(request));
    assert(aerVdbTransportRead(transport,&response,1)==1);
    return response;
}

static void test_native_activation_contract(void)
{
    AerVdbTransport transport;
    SensorEvidence sensor={0};
    aerVdbTransportInit(&transport,1);
    aerVdbTransportEnableNativePolicy(&transport,sensor_writer,&sensor);
    assert(aerVdbTransportStart(&transport));

    assert(transact(&transport,0x7f,0,0)==0x00);
    assert(transact(&transport,0x01,0x30,0x7f)==0x11);
    assert(transact(&transport,0x7c,0,0x20)==0x00);
    assert(transact(&transport,0x7a,0,0x1f)==0x00);
    assert(transact(&transport,0x03,0x32,0x04)==0x00);
    assert(transact(&transport,0x06,0x01,0x02)==0x00);
    assert(transact(&transport,0x08,0,0x04)==0x00);
    assert(transport.lifecycle==AER_VDB_CONFIGURING);

    assert(transact(&transport,0x00,1,1)==0x00);
    assert(transact(&transport,0x04,1,0x20)==0x00);
    assert(sensor.writes==1&&sensor.board==0&&sensor.position==0);
    assert(transport.lifecycle==AER_VDB_CALIBRATING);

    assert(transact(&transport,0x00,0,0)==0x00);
    assert(transact(&transport,0x01,0x32,0x7f)==0x00);
    assert(transact(&transport,0x1d,0,0)==0x00);
    assert(transact(&transport,0x1e,0,0)==0x00);
    assert(transport.lifecycle==AER_VDB_READY);
    for(int i=0;i<2000;i++)assert(aerVdbTransportTick(&transport));
    assert(transport.lifecycle==AER_VDB_READY);

    /* A representative native runtime family is transport data, not interpreted force. */
    assert(transact(&transport,0x02,0x01,0x28)==0x00);
    assert(transport.acceptedFrames==14);
    assert(transport.nativeCommandFrames==14);
    assert(transport.requestCount==0&&transport.responseCount==0);
    assert(!transport.physicalOutputAccessed);
}

static void test_unknown_initialization_command_fails_closed(void)
{
    AerVdbTransport transport;uint8_t request[4];
    aerVdbTransportInit(&transport,1);
    aerVdbTransportEnableNativePolicy(&transport,NULL,NULL);
    assert(aerVdbTransportStart(&transport));
    frame(request,0x22,0,0);
    errno=0;
    assert(aerVdbTransportWrite(&transport,request,sizeof(request))==-1);
    assert(errno==EPROTO&&transport.lifecycle==AER_VDB_FAULT);
    assert(!transport.physicalOutputAccessed);
}

static void test_dual_board_frame(void)
{
    AerVdbTransport transport;uint8_t request[7],responses[2];
    aerVdbTransportInit(&transport,2);
    aerVdbTransportEnableNativePolicy(&transport,NULL,NULL);
    assert(aerVdbTransportStart(&transport));
    request[0]=0xff;request[1]=0;request[2]=0;
    request[3]=0x7f;request[4]=0;request[5]=0;
    request[6]=0;
    assert(aerVdbTransportWrite(&transport,request,sizeof(request))==(ssize_t)sizeof(request));
    assert(aerVdbTransportRead(&transport,responses,sizeof(responses))==2);
    assert(responses[0]==0&&responses[1]==0);
    assert(!transport.physicalOutputAccessed);
}

static void test_captured_dev5_first_write_contract(void)
{
    static const uint8_t captured[7]={0xff,0x00,0x00,0x7d,0x00,0x00,0x02};
    AerVdbTransport transport;uint8_t responses[2]={0xff,0xff};

    /* DEV 5's former single-slot configuration reproduces the live rejection. */
    aerVdbTransportInit(&transport,1);
    aerVdbTransportEnableNativePolicy(&transport,NULL,NULL);
    assert(aerVdbTransportStart(&transport));
    errno=0;
    assert(aerVdbTransportWrite(&transport,captured,sizeof(captured))==-1);
    assert(errno==ENOBUFS&&transport.lifecycle==AER_VDB_FAULT);
    assert(transport.acceptedFrames==0);

    /* Jennifer's observed contract contains two command slots and one XOR byte. */
    aerVdbTransportInit(&transport,2);
    aerVdbTransportEnableNativePolicy(&transport,NULL,NULL);
    assert(aerVdbTransportStart(&transport));
    assert(aerVdbTransportWrite(&transport,captured,sizeof(captured))==(ssize_t)sizeof(captured));
    assert(aerVdbTransportRead(&transport,responses,sizeof(responses))==2);
    assert(responses[0]==0&&responses[1]==0);
    assert(transport.acceptedFrames==1&&transport.nativeCommandFrames==2);
    assert(transport.lifecycle==AER_VDB_INITIALIZING);
    assert(!transport.physicalOutputAccessed);
}

int main(void)
{
    test_native_activation_contract();
    test_unknown_initialization_command_fails_closed();
    test_dual_board_frame();
    test_captured_dev5_first_write_contract();
    puts("AER-02H DEV 5 activation transport: all tests passed");
    return 0;
}
