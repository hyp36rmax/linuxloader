#include "aerVirtualDriveboardBootstrap.h"

#include <stdio.h>
#include <string.h>

/*
 * Verified against Jennifer SHA-256
 * f16fc04d836a2bd8e401d8f987d4fe694fa16e18a9f870da6623d7f884911075.
 * This module is not referenced by LinuxLoader runtime initialization.
 */
static const AerVdbOriginalSite g_sites[] = {
    {0x08105317,6,{0x0f,0x84,0x6f,0x01,0x00,0x00},"SetOutFactor","base jump e91f000000 overwrites first 5 bytes"},
    {0x08109593,2,{0x78,0x43},"hmmInitInternal","base patch 9090"},
    {0x08109597,2,{0x78,0x3f},"hmmInitInternal","base patch 9090"},
    {0x0810959d,2,{0x7f,0x22},"hmmInitInternal","base patch changes opcode to 77"},
    {0x081e2180,4,{0xdb,0x41,0x10,0x08},"CabinetCtrl_InitDriver state table","emulator patch changes low word to df43"},
    {0x0810401b,1,{0x0b},"CabinetCtrl_InitDriver","emulator patch changes state 11 to 12"},
    {0x08103eaa,6,{0x55,0x89,0xe5,0x83,0xec,0x28},"CabinetCtrl_InitDriver","cabinet bypass detour"},
    {0x08105d88,6,{0x55,0x89,0xe5,0x83,0xec,0x18},"hardacuIsInitEnd","cabinet/emulator return-one detour"},
    {0x0810477e,6,{0x55,0x89,0xe5,0x83,0xec,0x28},"CabinetCtrl_Check","must remain original"},
    {0x081048b2,6,{0x55,0x89,0xe5,0x57,0x56,0x53},"CabinetCtrl_Main","must remain original"},
    {0x08104f02,6,{0x55,0x89,0xe5,0x57,0x56,0x53},"DrCtrlDataSet","must remain original"},
    {0x081051f4,6,{0x55,0x89,0xe5,0x83,0xec,0x18},"DrCtrlMoveSend","must remain original"},
    {0x08105ad2,6,{0x55,0x89,0xe5,0x57,0x56,0x53},"steerReqSendOut","must remain original"},
    {0x0810735e,6,{0x55,0x89,0xe5,0x8b,0x4d,0x08},"hardcomSend","must remain original"}
};

typedef struct Elf32Header
{
    uint8_t ident[16]; uint16_t type,machine; uint32_t version,entry,phoff,shoff,flags;
    uint16_t ehsize,phentsize,phnum,shentsize,shnum,shstrndx;
} Elf32Header;
typedef struct Elf32Program
{
    uint32_t type,offset,vaddr,paddr,filesz,memsz,flags,align;
} Elf32Program;

const AerVdbOriginalSite *aerVdbOriginalManifest(size_t *count)
{
    if(count)*count=sizeof(g_sites)/sizeof(g_sites[0]);
    return g_sites;
}

static int readVirtualBytes(const char *path,uint32_t address,uint8_t *output,size_t size)
{
    FILE *f;Elf32Header h;uint16_t i;
    if(!path||!output||!(f=fopen(path,"rb")))return 0;
    if(fread(&h,1,sizeof(h),f)!=sizeof(h)||memcmp(h.ident,"\x7f""ELF",4)||h.ident[4]!=1||h.ident[5]!=1||h.phentsize<sizeof(Elf32Program)){fclose(f);return 0;}
    for(i=0;i<h.phnum;i++){
        Elf32Program p;uint64_t end=(uint64_t)address+size;
        if(fseek(f,(long)(h.phoff+(uint32_t)i*h.phentsize),SEEK_SET)||fread(&p,1,sizeof(p),f)!=sizeof(p))break;
        if(p.type==1&&address>=p.vaddr&&end<=(uint64_t)p.vaddr+p.filesz){long off=(long)(p.offset+(address-p.vaddr));if(!fseek(f,off,SEEK_SET)&&fread(output,1,size,f)==size){fclose(f);return 1;}break;}
    }
    fclose(f);return 0;
}

void aerVdbBootstrapReset(AerVdbBootstrap *b){if(b)memset(b,0,sizeof(*b));}

AerVdbBootstrapResult aerVdbBootstrapEvaluate(AerVdbBootstrap *b,const AerVdbBootstrapInput *in)
{
    AerVdbManifestEntry entries[sizeof(g_sites)/sizeof(g_sites[0])];
    uint8_t actual[sizeof(g_sites)/sizeof(g_sites[0])][8];
    AerVdbTarget target;AerVdbConfig config;size_t i,count=sizeof(g_sites)/sizeof(g_sites[0]);
    if(!b||!in)return AER_VDB_BOOTSTRAP_CONFIG_REJECTED;
    if(b->evaluated)return AER_VDB_BOOTSTRAP_ALREADY_EVALUATED;
    b->evaluated=1;b->result=AER_VDB_BOOTSTRAP_DISABLED;
    if(!in->requested)return b->result;
    memset(entries,0,sizeof(entries));memset(actual,0,sizeof(actual));
    for(i=0;i<count;i++){
        int complete=readVirtualBytes(in->executablePath,g_sites[i].address,actual[i],g_sites[i].size);
        entries[i].address=g_sites[i].address;entries[i].expected=g_sites[i].expected;entries[i].actual=actual[i];entries[i].size=g_sites[i].size;entries[i].complete=complete;
    }
    target=(AerVdbTarget){1,in->revision,in->crc32,in->executablePath,in->testExpectedSha256,entries,count};
    b->verification=aerVdbVerifyTarget(&target);
    if(b->verification!=AER_VDB_VERIFY_OK){b->result=AER_VDB_BOOTSTRAP_IDENTITY_REJECTED;return b->result;}
    config=(AerVdbConfig){1,in->skipCabinetCheck,in->emulateDriveboard,in->physicalSerialRequested,in->boardCount,b->verification};
    b->configuration=aerVdbValidateConfig(&config);
    if(b->configuration!=AER_VDB_CONFIG_OK){b->result=AER_VDB_BOOTSTRAP_CONFIG_REJECTED;return b->result;}
    if((in->bridgeCapabilities&AER_VDB_REQUIRED_BRIDGE_CAPABILITIES)!=AER_VDB_REQUIRED_BRIDGE_CAPABILITIES){b->result=AER_VDB_BOOTSTRAP_BRIDGE_REJECTED;return b->result;}
    b->eligible=1;b->result=AER_VDB_BOOTSTRAP_ELIGIBLE;
    return b->result;
}
