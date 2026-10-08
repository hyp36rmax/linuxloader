#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "loader/research/aerVirtualDriveboardBridge.h"

typedef struct TestElfHeader { uint8_t ident[16];uint16_t type,machine;uint32_t version,entry,phoff,shoff,flags;uint16_t ehsize,phentsize,phnum,shentsize,shnum,shstrndx; } TestElfHeader;
typedef struct TestElfProgram { uint32_t type,offset,vaddr,paddr,filesz,memsz,flags,align; } TestElfProgram;
static const uint32_t base=0x08000000u;

static void fixture(const char *path)
{
    const size_t size=0x001e3000u; FILE *f=fopen(path,"wb+"); TestElfHeader h; TestElfProgram p;
    const AerVdbOriginalSite *sites; size_t count,i;
    assert(f); assert(!fseek(f,(long)size-1,SEEK_SET)); assert(fputc(0,f)!=EOF);
    memset(&h,0,sizeof(h)); memcpy(h.ident,"\x7f""ELF",4); h.ident[4]=1; h.ident[5]=1;
    h.phoff=sizeof(h); h.ehsize=sizeof(h); h.phentsize=sizeof(p); h.phnum=1;
    p=(TestElfProgram){1,0,base,base,(uint32_t)size,(uint32_t)size,5,0x1000};
    rewind(f); assert(fwrite(&h,1,sizeof(h),f)==sizeof(h)); assert(fwrite(&p,1,sizeof(p),f)==sizeof(p));
    sites=aerVdbOriginalManifest(&count);
    for(i=0;i<count;i++){ assert(!fseek(f,(long)(sites[i].address-base),SEEK_SET)); assert(fwrite(sites[i].expected,1,sites[i].size,f)==sites[i].size); }
    assert(!fclose(f));
}

static AerVdbBridgeRequest request(const char *path,const char *hash)
{
    AerVdbBridgeRequest r; memset(&r,0,sizeof(r)); r.requested=1; r.revision=AER_VDB_EXPECTED_REVISION;
    r.crc32=AER_VDB_EXPECTED_CRC32; r.executablePath=path; r.boardCount=1;
    r.capabilities=AER_VDB_REQUIRED_BRIDGE_CAPABILITIES|AER_VDB_BRIDGE_WRITEV|AER_VDB_BRIDGE_DUP;
    r.testExpectedSha256=hash; return r;
}

static void start(const AerVdbBridgeRequest *r)
{
    aerVdbBridgeReset(); assert(aerVdbBridgeInitialize(r)==AER_VDB_BOOTSTRAP_ELIGIBLE);
    assert(aerVdbBridgeEligible()); assert(aerVdbBridgeAttach(50));
}

int main(int argc,char **argv)
{
    char hash[65],byte=0; int queued=-1; const uint8_t frame[4]={0x80,1,2,3};
    const void *parts[2]={frame,frame+2}; size_t sizes[2]={2,2};
    assert(argc==2); fixture(argv[1]); assert(aerVdbSha256File(argv[1],hash));
    AerVdbBridgeRequest r=request(argv[1],hash),off=r;
    off.requested=0; aerVdbBridgeReset(); assert(aerVdbBridgeInitialize(&off)==AER_VDB_BOOTSTRAP_DISABLED); assert(!aerVdbBridgeEligible());
    start(&r); assert(aerVdbBridgeDup(50,51)); assert(aerVdbBridgeWrite(50,frame,2)==2); assert(aerVdbBridgeWrite(51,frame+2,2)==2);
    assert(!aerVdbBridgeReadable(50)); assert(aerVdbBridgeWritable(51));
    assert(aerVdbBridgeQueueTestResponse(0x11)); assert(aerVdbBridgeQueueTestResponse(0x22)); assert(aerVdbBridgeReadable(51));
    assert(aerVdbBridgeIoctl(51,0x541b,&queued)==0&&queued==2); assert(aerVdbBridgeRead(50,&byte,1)==1&&byte==0x11);
    assert(aerVdbBridgeIoctl(51,0x541b,&queued)==0&&queued==1); assert(aerVdbBridgeRead(50,&byte,1)==1&&byte==0x22);
    errno=0; assert(aerVdbBridgeRead(50,&byte,1)==-1&&errno==EAGAIN);
    assert(aerVdbBridgeWritev(51,parts,sizes,2)==4); assert(aerVdbBridgeFwrite(50,frame,2,2)==2);
    assert(aerVdbBridgeClose(50)); assert(aerVdbBridgeContains(51)); assert(aerVdbBridgeClose(51)); assert(!aerVdbBridgeContains(51));
    start(&r); for(int i=0;i<AER_VDB_QUEUE_CAPACITY;i++) assert(aerVdbBridgeWrite(50,frame,4)==4);
    assert(aerVdbBridgeWrite(50,frame,4)==-1); aerVdbBridgeShutdown(); aerVdbBridgeShutdown();
    start(&r); for(int i=0;i<900;i++) assert(aerVdbBridgeTick()); assert(!aerVdbBridgeTick());
    start(&r); aerVdbBridgeDisconnect(); assert(aerVdbBridgeWrite(50,frame,4)==-1);
    r.skipCabinetCheck=1; aerVdbBridgeReset(); assert(aerVdbBridgeInitialize(&r)==AER_VDB_BOOTSTRAP_CONFIG_REJECTED); assert(!aerVdbBridgeEligible());
    r=request("/missing/Jennifer",hash); aerVdbBridgeReset(); assert(aerVdbBridgeInitialize(&r)==AER_VDB_BOOTSTRAP_IDENTITY_REJECTED);
    puts("AER-02H.3 virtual drive-board bridge: all tests passed"); return 0;
}
