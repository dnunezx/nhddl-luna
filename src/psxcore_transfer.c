// Original bounded ELF32/MIPS bootstrap validator. Does not grant file trust.
#include "luna_psxcore.h"
#include <string.h>
static uint32_t u32(const unsigned char *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static unsigned u16(const unsigned char *p) {return p[0]|(unsigned)p[1]<<8;}
const char *lunaPsxArgumentsCheck(const char *bootstrap,unsigned argumentCount,
    const char *const arguments[],LunaPsxTransfer *out) {
    if(!bootstrap || !out || argumentCount>=LUNA_PSX_MAX_ARGS ||
       (argumentCount && !arguments))return "launch-arguments";
    LunaPsxTransfer result={0};size_t used=0;
    for(unsigned i=0;i<=argumentCount;++i) {
        const char *value=i?arguments[i-1]:bootstrap;
        if(!value || !value[0])return "launch-argument-empty";
        size_t bytes=0;
        while(bytes<LUNA_PSX_ARGUMENT_BYTES && value[bytes])++bytes;
        if(bytes==LUNA_PSX_ARGUMENT_BYTES || bytes+1>sizeof(result.strings)-used)
            return "launch-arguments-size";
        result.argv[i]=LUNA_PSX_CONTROL+offsetof(LunaPsxTransfer,strings)+(uint32_t)used;
        memcpy(result.strings+used,value,bytes+1);used+=bytes+1;
    }
    result.argc=argumentCount+1;*out=result;return NULL;
}
const char *lunaPsxBootstrapCheck(const void *bytes,size_t size,NpElf *out) {
    const unsigned char *p=bytes;NpElf result={0};int executable=0;
    if(!p || !out || size<52 || size>LUNA_PSX_CAPACITY)return "bootstrap-size";
    if(memcmp(p,"\177ELF",4) || p[4]!=1 || p[5]!=1 || p[6]!=1 || p[7] ||
       u16(p+16)!=2 || u16(p+18)!=8 || u32(p+20)!=1 ||
       u16(p+40)!=52 || u16(p+42)!=32)return "bootstrap-header";
    unsigned n=u16(p+44);uint32_t ph=u32(p+28);
    if(!n || n>32 || ph<52 || ph>size || n>(size-ph)/32)return "bootstrap-program-headers";
    result.entry=u32(p+24);
    if(result.entry&3)return "bootstrap-entry-alignment";
    for(unsigned i=0;i<n;++i) {
        const unsigned char *h=p+ph+i*32;
        uint32_t type=u32(h),off=u32(h+4),addr=u32(h+8),phys=u32(h+12);
        uint32_t files=u32(h+16),mem=u32(h+20),flags=u32(h+24),align=u32(h+28);
        if(!type)continue;
        if(type==0x70000000u || type==0x70000003u) {
            if(off>size || files>size-off)return "bootstrap-metadata-size";
            continue;
        }
        if(type!=1)return "bootstrap-segment-type";
        if(!mem || files>mem || off>size || files>size-off)return "bootstrap-segment-size";
        // Low resident hooks and normal bootstrap code only. No kernel aliases,
        // frontend transfer storage, firmware staging, or scratchpad executable.
        if(addr<0x000f0000u || addr>=0x00800000u || mem>0x00800000u-addr || phys!=addr)
            return "bootstrap-segment-address";
        if(flags&~7u)return "bootstrap-segment-flags";
        if(align>1 && ((align&(align-1)) || (addr&(align-1))!=(off&(align-1))))
            return "bootstrap-segment-alignment";
        if(result.count==NP_ELF_MAX_SEGMENTS)return "bootstrap-segment-count";
        for(unsigned j=0;j<result.count;++j) {
            NpSegment *s=&result.segments[j];
            if(addr<s->address+s->memsz && s->address<addr+mem)return "bootstrap-segment-overlap";
        }
        result.segments[result.count++]=(NpSegment){off,addr,files,mem};
        if((flags&1) && result.entry>=addr && result.entry-addr<files && result.entry>=0x00100000u)
            executable=1;
    }
    if(!result.count || !executable)return "bootstrap-entry-unmapped";
    *out=result;return NULL;
}
