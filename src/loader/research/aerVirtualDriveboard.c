#include "aerVirtualDriveboard.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* Standalone research infrastructure. This file is intentionally not linked into LinuxLoader. */

typedef struct Sha256Context { uint32_t h[8]; uint64_t bits; uint8_t block[64]; size_t used; } Sha256Context;
static uint32_t rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
static void shaBlock(Sha256Context *c, const uint8_t *p)
{
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t w[64], a,b,d,e,f,g,h,i,t1,t2,cc;
    for (i=0;i<16;i++) w[i]=((uint32_t)p[i*4]<<24)|((uint32_t)p[i*4+1]<<16)|((uint32_t)p[i*4+2]<<8)|p[i*4+3];
    for (;i<64;i++) { uint32_t s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3); uint32_t s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10); w[i]=w[i-16]+s0+w[i-7]+s1; }
    a=c->h[0]; b=c->h[1]; cc=c->h[2]; d=c->h[3]; e=c->h[4]; f=c->h[5]; g=c->h[6]; h=c->h[7];
    for (i=0;i<64;i++) { uint32_t s1=rotr(e,6)^rotr(e,11)^rotr(e,25); uint32_t ch=(e&f)^((~e)&g); t1=h+s1+ch+k[i]+w[i]; uint32_t s0=rotr(a,2)^rotr(a,13)^rotr(a,22); uint32_t maj=(a&b)^(a&cc)^(b&cc); t2=s0+maj; h=g;g=f;f=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2; }
    c->h[0]+=a;c->h[1]+=b;c->h[2]+=cc;c->h[3]+=d;c->h[4]+=e;c->h[5]+=f;c->h[6]+=g;c->h[7]+=h;
}
static void shaInit(Sha256Context *c) { static const uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}; memcpy(c->h,h,sizeof(h)); c->bits=0;c->used=0; }
static void shaUpdate(Sha256Context *c,const uint8_t *p,size_t n) { c->bits+=(uint64_t)n*8; while(n){size_t m=64-c->used;if(m>n)m=n;memcpy(c->block+c->used,p,m);c->used+=m;p+=m;n-=m;if(c->used==64){shaBlock(c,c->block);c->used=0;}} }
static void shaFinal(Sha256Context *c,uint8_t out[32]) { size_t i;c->block[c->used++]=0x80;if(c->used>56){memset(c->block+c->used,0,64-c->used);shaBlock(c,c->block);c->used=0;}memset(c->block+c->used,0,56-c->used);for(i=0;i<8;i++)c->block[63-i]=(uint8_t)(c->bits>>(i*8));shaBlock(c,c->block);for(i=0;i<8;i++){out[i*4]=(uint8_t)(c->h[i]>>24);out[i*4+1]=(uint8_t)(c->h[i]>>16);out[i*4+2]=(uint8_t)(c->h[i]>>8);out[i*4+3]=(uint8_t)c->h[i];}}

int aerVdbSha256File(const char *path, char output[65])
{
    FILE *f; uint8_t data[4096], digest[32]; size_t n, i; Sha256Context c;
    if (!path || !output || !(f=fopen(path,"rb"))) return 0;
    shaInit(&c); while ((n=fread(data,1,sizeof(data),f))>0) shaUpdate(&c,data,n);
    if (ferror(f)) { fclose(f); return 0; } fclose(f); shaFinal(&c,digest);
    for (i = 0; i < 32; i++) {
        sprintf(output + i * 2, "%02x", digest[i]);
    }
    output[64] = '\0';
    return 1;
}

AerVdbVerifyResult aerVdbVerifyTarget(const AerVdbTarget *t)
{
    char hash[65]; size_t i; const char *expected=AER_VDB_EXPECTED_SHA256;
    if (!t || !t->requested) return AER_VDB_VERIFY_DISABLED;
    if (!t->revision || strcmp(t->revision,AER_VDB_EXPECTED_REVISION)) return AER_VDB_VERIFY_WRONG_REVISION;
    if (t->crc32!=AER_VDB_EXPECTED_CRC32) return AER_VDB_VERIFY_WRONG_CRC;
#ifdef AER_VDB_TESTING
    if (t->testExpectedSha256) expected=t->testExpectedSha256;
#else
    (void)t->testExpectedSha256;
#endif
    if (!aerVdbSha256File(t->executablePath,hash)) return AER_VDB_VERIFY_MISSING_EXECUTABLE;
    if (strcmp(hash,expected)) return AER_VDB_VERIFY_WRONG_SHA256;
    if (!t->manifest || t->manifestCount==0) return AER_VDB_VERIFY_INCOMPLETE_MANIFEST;
    for(i=0;i<t->manifestCount;i++) {
        const AerVdbManifestEntry *e=&t->manifest[i];
        if (!e->complete || !e->expected || !e->actual || e->size==0) return AER_VDB_VERIFY_INCOMPLETE_MANIFEST;
        if (memcmp(e->expected,e->actual,e->size)) return AER_VDB_VERIFY_BYTE_MISMATCH;
    }
    return AER_VDB_VERIFY_OK;
}

AerVdbConfigResult aerVdbValidateConfig(const AerVdbConfig *c)
{
    if(!c||!c->requested) return AER_VDB_CONFIG_DISABLED;
    if(c->skipCabinetCheck) return AER_VDB_CONFIG_SKIP_CONFLICT;
    if(c->emulateDriveboard) return AER_VDB_CONFIG_EMULATOR_CONFLICT;
    if(c->physicalSerialRequested) return AER_VDB_CONFIG_PHYSICAL_CONFLICT;
    if(c->boardCount<1||c->boardCount>AER_VDB_MAX_BOARDS) return AER_VDB_CONFIG_INVALID_BOARD_COUNT;
    if(c->verification!=AER_VDB_VERIFY_OK) return c->verification==AER_VDB_VERIFY_INCOMPLETE_MANIFEST?AER_VDB_CONFIG_INCOMPLETE_VERIFICATION:AER_VDB_CONFIG_UNSUPPORTED_TARGET;
    return AER_VDB_CONFIG_OK;
}

static void fault(AerVdbTransport *t) { t->lifecycle=AER_VDB_FAULT;t->requestCount=t->responseCount=t->partialCount=0; }
void aerVdbTransportInit(AerVdbTransport *t,int boards) { memset(t,0,sizeof(*t));t->boardCount=boards;t->lifecycle=(boards>=1&&boards<=2)?AER_VDB_IDLE:AER_VDB_FAULT;t->sensorStep=10; }
void aerVdbTransportEnableNativePolicy(AerVdbTransport *t,AerVdbSensorWriter writer,void *context)
{
    if(!t)return;
    t->nativeResponsePolicy=1;
    t->sensorWriter=writer;
    t->sensorWriterContext=context;
}
int aerVdbTransportStart(AerVdbTransport *t) { if(!t||t->lifecycle!=AER_VDB_IDLE)return 0;t->lifecycle=AER_VDB_INITIALIZING;t->timeoutTicks=0;return 1; }
static size_t frameSize(const AerVdbTransport *t){return t->boardCount==2?7u:4u;}
static int validFrame(const uint8_t *p,size_t n) { size_t i;uint8_t x=0;if(n!=4&&n!=7)return 0;for(i=0;i<n-1;i++)x^=(uint8_t)(p[i]&((i==0||i==3)?0x7f:0xff));return x==p[n-1];}
static int queueResponse(AerVdbTransport *t,uint8_t response)
{
    size_t cap=AER_VDB_QUEUE_CAPACITY*AER_VDB_MAX_BOARDS;
    if(t->responseCount>=cap){fault(t);errno=ENOBUFS;return 0;}
    t->responses[(t->responseHead+t->responseCount)%cap]=response;
    t->responseCount++;
    return 1;
}
static int applyNativeRequest(AerVdbTransport *t,int board,const uint8_t *request)
{
    uint8_t command=(uint8_t)(request[0]&0x7f),response=0x00;
    switch(command){
        case 0x7f:
            t->lifecycle=AER_VDB_INITIALIZING;
            break;
        case 0x01:
            t->lifecycle=AER_VDB_INITIALIZING;
            if(request[1]==0x30&&request[2]==0x7f)response=0x11;
            break;
        case 0x7c: case 0x7d:
            t->lifecycle=AER_VDB_INITIALIZING;
            break;
        case 0x7a: case 0x03: case 0x06: case 0x08:
            t->lifecycle=AER_VDB_CONFIGURING;
            break;
        case 0x00: case 0x04: case 0x70:
            t->lifecycle=AER_VDB_CALIBRATING;
            if(command==0x04&&t->sensorWriter){
                t->sensorPosition=t->sensorCenter;
                t->sensorTarget=t->sensorCenter;
                t->sensorWriter(board,t->sensorCenter,t->sensorWriterContext);
            }
            break;
        case 0x1d: case 0x1e:
            t->lifecycle=AER_VDB_READY;
            break;
        default:
            if(t->lifecycle!=AER_VDB_READY){fault(t);errno=EPROTO;return 0;}
            break;
    }
    t->nativeCommandFrames++;
    return queueResponse(t,response);
}
static int processNativeFrame(AerVdbTransport *t,const AerVdbFrame *frame)
{
    int board;
    for(board=0;board<t->boardCount;board++)
        if(!applyNativeRequest(t,board,frame->bytes+(size_t)board*3u))return 0;
    return 1;
}
ssize_t aerVdbTransportWrite(AerVdbTransport *t,const void *data,size_t size)
{
    const uint8_t *p=data;size_t i,n;
    if(!t||(!data&&size)){errno=EINVAL;return -1;} if(t->lifecycle==AER_VDB_FAULT||t->lifecycle==AER_VDB_SHUTDOWN||t->disconnected){errno=EIO;return -1;}
    n=frameSize(t); if(t->requestCount>=AER_VDB_QUEUE_CAPACITY||t->partialCount+size>n){fault(t);errno=ENOBUFS;return -1;}
    for(i=0;i<size;i++)t->partial[t->partialCount++]=p[i];
    if(t->partialCount==n){AerVdbFrame *f;if(!validFrame(t->partial,n)){fault(t);errno=EPROTO;return -1;}f=&t->requests[(t->requestHead+t->requestCount)%AER_VDB_QUEUE_CAPACITY];memcpy(f->bytes,t->partial,n);f->size=n;t->requestCount++;t->partialCount=0;t->acceptedFrames++;t->timeoutTicks=0;if(t->nativeResponsePolicy){if(!processNativeFrame(t,f))return -1;t->requestHead=(t->requestHead+1)%AER_VDB_QUEUE_CAPACITY;t->requestCount--;}}
    return (ssize_t)size;
}
ssize_t aerVdbTransportWritev(AerVdbTransport *t,const void *const *data,const size_t *sizes,size_t count){size_t i,total=0;AerVdbTransport copy;if(!t||(!data&&count)||(!sizes&&count)){errno=EINVAL;return -1;}for(i=0;i<count;i++){if(!data[i]&&sizes[i]){errno=EINVAL;return -1;}if(SIZE_MAX-total<sizes[i]||total+sizes[i]>frameSize(t)-t->partialCount){errno=ENOBUFS;return -1;}total+=sizes[i];}copy=*t;for(i=0;i<count;i++)if(aerVdbTransportWrite(&copy,data[i],sizes[i])<0)return -1;*t=copy;return (ssize_t)total;}
size_t aerVdbTransportReadable(const AerVdbTransport *t){return t?t->responseCount:0;}
ssize_t aerVdbTransportRead(AerVdbTransport *t,void *data,size_t size){size_t i,n;if(!t||(!data&&size)){errno=EINVAL;return -1;}if(t->lifecycle==AER_VDB_FAULT||t->lifecycle==AER_VDB_SHUTDOWN){errno=EIO;return -1;}if(!t->responseCount){errno=EAGAIN;return -1;}n=size<t->responseCount?size:t->responseCount;for(i=0;i<n;i++)((uint8_t*)data)[i]=t->responses[(t->responseHead+i)%(AER_VDB_QUEUE_CAPACITY*AER_VDB_MAX_BOARDS)];t->responseHead=(t->responseHead+n)%(AER_VDB_QUEUE_CAPACITY*AER_VDB_MAX_BOARDS);t->responseCount-=n;t->timeoutTicks=0;return(ssize_t)n;}
size_t aerVdbTransportFwrite(AerVdbTransport *t,const void *data,size_t es,size_t ec){size_t bytes;if(!es||!ec)return 0;if(ec>SIZE_MAX/es){errno=EOVERFLOW;return 0;}bytes=es*ec;ssize_t n=aerVdbTransportWrite(t,data,bytes);return n<0?0:(size_t)n/es;}
int aerVdbTransportQueueAssumedResponse(AerVdbTransport *t,uint8_t r){if(!t)return 0;return queueResponse(t,r);}
int aerVdbTransportTick(AerVdbTransport *t){if(!t||t->lifecycle==AER_VDB_FAULT||t->lifecycle==AER_VDB_SHUTDOWN)return 0;if(++t->timeoutTicks>900){fault(t);return 0;}return 1;}
int aerVdbSensorRequest(AerVdbTransport *t,int direction){if(!t||t->lifecycle!=AER_VDB_CALIBRATING||(direction!=-1&&direction!=1)){if(t)fault(t);return 0;}t->sensorTarget=t->sensorPosition+direction*10;if(t->sensorTarget>96)t->sensorTarget=96;if(t->sensorTarget< -96)t->sensorTarget=-96;return 1;}
int aerVdbSensorTick(AerVdbTransport *t){if(!t||t->lifecycle!=AER_VDB_CALIBRATING)return 0;if(++t->calibrationTicks>900){fault(t);return 0;}if(t->sensorPosition<t->sensorTarget)t->sensorPosition+=t->sensorStep;else if(t->sensorPosition>t->sensorTarget)t->sensorPosition-=t->sensorStep;if(t->sensorPosition>96)t->sensorPosition=96;if(t->sensorPosition< -96)t->sensorPosition=-96;return t->sensorPosition==t->sensorTarget;}
void aerVdbTransportDisconnect(AerVdbTransport *t){if(t){t->disconnected=1;fault(t);}}
void aerVdbTransportShutdown(AerVdbTransport *t){if(!t)return;memset(t->requests,0,sizeof(t->requests));memset(t->responses,0,sizeof(t->responses));t->requestHead=t->requestCount=t->responseHead=t->responseCount=t->partialCount=0;t->sensorPosition=t->sensorCenter=t->sensorTarget=0;t->lifecycle=AER_VDB_SHUTDOWN;}

void aerVdbRegistryInit(AerVdbDescriptorRegistry *r,AerVdbTransport *t){memset(r,0,sizeof(*r));r->transport=t;r->nextGeneration=1;}
int aerVdbRegistryOpen(AerVdbDescriptorRegistry *r,int fd){size_t i;if(!r||fd<0||aerVdbRegistryLookup(r,fd)>=0)return 0;for(i=0;i<AER_VDB_MAX_DESCRIPTORS;i++)if(!r->slots[i].inUse){r->slots[i].fd=fd;r->slots[i].generation=r->nextGeneration++;r->slots[i].inUse=1;r->openCount++;return 1;}return 0;}
int aerVdbRegistryLookup(const AerVdbDescriptorRegistry *r,int fd){size_t i;if(!r)return-1;for(i=0;i<AER_VDB_MAX_DESCRIPTORS;i++)if(r->slots[i].inUse&&r->slots[i].fd==fd)return(int)i;return-1;}
int aerVdbRegistryDup(AerVdbDescriptorRegistry *r,int src,int dst){if(aerVdbRegistryLookup(r,src)<0)return 0;return aerVdbRegistryOpen(r,dst);}
int aerVdbRegistryClose(AerVdbDescriptorRegistry *r,int fd){int i=aerVdbRegistryLookup(r,fd);if(i<0)return 0;r->slots[i].inUse=0;r->slots[i].fd=-1;r->openCount--;if(!r->openCount)aerVdbTransportShutdown(r->transport);return 1;}
