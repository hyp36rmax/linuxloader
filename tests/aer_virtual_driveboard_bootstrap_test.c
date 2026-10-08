#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "loader/research/aerVirtualDriveboardBootstrap.h"

typedef struct TestElfHeader { uint8_t ident[16];uint16_t type,machine;uint32_t version,entry,phoff,shoff,flags;uint16_t ehsize,phentsize,phnum,shentsize,shnum,shstrndx; } TestElfHeader;
typedef struct TestElfProgram { uint32_t type,offset,vaddr,paddr,filesz,memsz,flags,align; } TestElfProgram;

static const uint32_t base=0x08000000u;
static const size_t fixtureSize=0x001e3000u;

static void createFixture(const char *path,int truncateManifest)
{
    FILE *f=fopen(path,"wb+");TestElfHeader h;TestElfProgram p;const AerVdbOriginalSite *sites;size_t count,i,size=truncateManifest?0x00100000u:fixtureSize;
    assert(f);assert(!fseek(f,(long)size-1,SEEK_SET));assert(fputc(0,f)!=EOF);
    memset(&h,0,sizeof(h));memcpy(h.ident,"\x7f""ELF",4);h.ident[4]=1;h.ident[5]=1;h.phoff=sizeof(h);h.ehsize=sizeof(h);h.phentsize=sizeof(p);h.phnum=1;
    p=(TestElfProgram){1,0,base,base,(uint32_t)size,(uint32_t)size,5,0x1000};
    rewind(f);assert(fwrite(&h,1,sizeof(h),f)==sizeof(h));assert(fwrite(&p,1,sizeof(p),f)==sizeof(p));
    sites=aerVdbOriginalManifest(&count);for(i=0;i<count;i++)if((size_t)(sites[i].address-base)+sites[i].size<=size){assert(!fseek(f,(long)(sites[i].address-base),SEEK_SET));assert(fwrite(sites[i].expected,1,sites[i].size,f)==sites[i].size);}
    assert(!fclose(f));
}

static AerVdbBootstrapInput validInput(const char *path,const char *hash)
{
    AerVdbBootstrapInput in={1,AER_VDB_EXPECTED_REVISION,AER_VDB_EXPECTED_CRC32,path,hash,0,0,0,1,AER_VDB_REQUIRED_BRIDGE_CAPABILITIES};return in;
}

static void expect(AerVdbBootstrapInput in,AerVdbBootstrapResult wanted)
{
    AerVdbBootstrap b;memset(&b,0,sizeof(b));assert(aerVdbBootstrapEvaluate(&b,&in)==wanted);assert(b.mutationCount==0);assert(b.eligible==(wanted==AER_VDB_BOOTSTRAP_ELIGIBLE));
}

int main(int argc,char **argv)
{
    char hash[65],modifiedHash[65],shortHash[65];AerVdbBootstrapInput in;AerVdbBootstrap b;FILE *f;uint8_t byte;
    assert(argc==3);createFixture(argv[1],0);assert(aerVdbSha256File(argv[1],hash));in=validInput(argv[1],hash);
    expect(in,AER_VDB_BOOTSTRAP_ELIGIBLE);
    in.requested=0;expect(in,AER_VDB_BOOTSTRAP_DISABLED);in=validInput(argv[1],hash);
    in.revision="DVP-0015";expect(in,AER_VDB_BOOTSTRAP_IDENTITY_REJECTED);in=validInput(argv[1],hash);
    in.crc32=0;expect(in,AER_VDB_BOOTSTRAP_IDENTITY_REJECTED);in=validInput(argv[1],hash);
    in.testExpectedSha256="00";expect(in,AER_VDB_BOOTSTRAP_IDENTITY_REJECTED);in=validInput("/missing/Jennifer",hash);expect(in,AER_VDB_BOOTSTRAP_IDENTITY_REJECTED);
    in=validInput(argv[1],hash);in.skipCabinetCheck=1;expect(in,AER_VDB_BOOTSTRAP_CONFIG_REJECTED);in=validInput(argv[1],hash);in.emulateDriveboard=1;expect(in,AER_VDB_BOOTSTRAP_CONFIG_REJECTED);
    in=validInput(argv[1],hash);in.physicalSerialRequested=1;expect(in,AER_VDB_BOOTSTRAP_CONFIG_REJECTED);in=validInput(argv[1],hash);in.boardCount=0;expect(in,AER_VDB_BOOTSTRAP_CONFIG_REJECTED);
    in=validInput(argv[1],hash);in.bridgeCapabilities&=~AER_VDB_BRIDGE_IOCTL;expect(in,AER_VDB_BOOTSTRAP_BRIDGE_REJECTED);
    f=fopen(argv[1],"rb+");assert(f);assert(!fseek(f,(long)(0x0810401b-base),SEEK_SET));assert(fread(&byte,1,1,f)==1);assert(!fseek(f,-1,SEEK_CUR));byte^=1;assert(fwrite(&byte,1,1,f)==1);assert(!fclose(f));assert(aerVdbSha256File(argv[1],modifiedHash));in=validInput(argv[1],modifiedHash);expect(in,AER_VDB_BOOTSTRAP_IDENTITY_REJECTED);
    createFixture(argv[2],1);assert(aerVdbSha256File(argv[2],shortHash));in=validInput(argv[2],shortHash);expect(in,AER_VDB_BOOTSTRAP_IDENTITY_REJECTED);
    createFixture(argv[1],0);assert(aerVdbSha256File(argv[1],hash));in=validInput(argv[1],hash);aerVdbBootstrapReset(&b);assert(aerVdbBootstrapEvaluate(&b,&in)==AER_VDB_BOOTSTRAP_ELIGIBLE);assert(aerVdbBootstrapEvaluate(&b,&in)==AER_VDB_BOOTSTRAP_ALREADY_EVALUATED);aerVdbBootstrapReset(&b);assert(aerVdbBootstrapEvaluate(&b,&in)==AER_VDB_BOOTSTRAP_ELIGIBLE);
    puts("AER-02H.2 virtual drive-board bootstrap: all tests passed");return 0;
}
