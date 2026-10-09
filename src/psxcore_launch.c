// Original LUNA PSXCore handoff, 2026.
#include "luna_psxcore.h"
#include "devices/devices.h"
#include "dprintf.h"
#include "ui/ambient.h"
#include "ui/art_cache.h"
#include "ui/handoff.h"
#include "ui/pad.h"
#include "ui/graphics.h"
#include <dmaKit.h>
#include <kernel.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
extern unsigned char luna_psx_stub_start[],luna_psx_stub_end[];
_Static_assert(offsetof(LunaPsxTransfer,argc)==136,"transfer argc offset");
_Static_assert(offsetof(LunaPsxTransfer,argv)==140,"transfer argv offset");
_Static_assert(LUNA_PSX_STAGE+LUNA_PSX_CAPACITY<=LUNA_PSX_CONTROL,"stage boundary");
_Static_assert(LUNA_PSX_CONTROL+sizeof(LunaPsxTransfer)<=LUNA_PSX_STUB,"plan boundary");
static int prepared;

const char *lunaPsxPrepare(const char *bootstrap,unsigned argumentCount,
    const char *const arguments[]) {
    prepared=0;
    uintptr_t stack;asm volatile("move %0,$sp":"=r"(stack));
    // Unlike a linker static-data limit, EndOfHeap bounds all SDK allocations,
    // including audio/art thread stacks. The PSXCore build sets a finite heap.
    if((unsigned)GetMemorySize()<0x02000000u || (uintptr_t)EndOfHeap()>LUNA_PSX_STAGE ||
       stack>=LUNA_PSX_STAGE)return "frontend-memory-reservation";
    LunaPsxTransfer plan;
    const char *error=lunaPsxArgumentsCheck(bootstrap,argumentCount,arguments,&plan);
    if(error)return error;
    FILE *f=fopen(bootstrap,"rb");if(!f)return "bootstrap-open";
    size_t n=fread((void *)LUNA_PSX_STAGE,1,LUNA_PSX_CAPACITY,f);
    int extra=fgetc(f),bad=ferror(f),closed=fclose(f);
    if(extra!=EOF || bad || closed)return "bootstrap-read-size";
    error=lunaPsxBootstrapCheck((void *)LUNA_PSX_STAGE,n,&plan.elf);
    if(error)return error;
    size_t stubBytes=luna_psx_stub_end-luna_psx_stub_start;
    if(!stubBytes || stubBytes>4096)return "transfer-stub-size";
    memcpy((void *)LUNA_PSX_CONTROL,&plan,sizeof(plan));
    memcpy((void *)LUNA_PSX_STUB,luna_psx_stub_start,stubBytes);
    prepared=1;
    DPRINTF("LUNA PSXCore handoff=prepared entry=%08x segments=%u argc=%u transport=argv\n",plan.elf.entry,plan.elf.count,plan.argc);
    return NULL;
}
void lunaPsxExecute(void) {
    if(!prepared)__builtin_trap();
    uiPlayLaunchTransition();
    ambientStop();
    // Finish queued GPU DMA before freeing art pixels; then join all artwork
    // workers before closing frontend I/O and replacing code or thread stacks.
    gsKit_queue_exec(gsGlobal);gsKit_finish();dmaKit_wait_fast();
    artCacheShutdown();closePad();syncDeviceMap();
    DPRINTF("LUNA PSXCore handoff=transfer frontend-workers=stopped\n");
    FlushCache(0);FlushCache(2);TerminateLibrary();DIntr();
    ((void (*)(LunaPsxTransfer *))LUNA_PSX_STUB)((LunaPsxTransfer *)LUNA_PSX_CONTROL);
    __builtin_trap();
}
#ifdef LUNA_PSXCORE_DEVELOPMENT
static int configLine(FILE *f,char *out,size_t size) {
    if(!fgets(out,size,f))return -1;
    size_t n=strlen(out);
    if(!n || (out[n-1]!='\n' && !feof(f)))return -1;
    out[strcspn(out,"\r\n")]=0;return out[0]?0:-1;
}
void lunaPsxDevelopment(int automatic) {
    FILE *f=fopen("host:/luna-psxcore-dev.txt","rb");
    if(!f){if(!automatic)DPRINTF("LUNA PSXCore development config missing\n");return;}
    char bootstrap[256],lines[LUNA_PSX_MAX_ARGS][256];
    const char *arguments[LUNA_PSX_MAX_ARGS];unsigned count=0;int autoLaunch=0;
    int bad=configLine(f,bootstrap,sizeof(bootstrap));
    while(!bad && !feof(f)) {
        int next=fgetc(f);if(next==EOF)break;ungetc(next,f);
        if(count>=LUNA_PSX_MAX_ARGS || configLine(f,lines[count],sizeof(lines[count]))) {bad=1;break;}
        if(!strcmp(lines[count],"auto")) {
            autoLaunch=1;if(fgetc(f)!=EOF)bad=1;break;
        }
        arguments[count]=lines[count];++count;
    }
    int io=ferror(f),closed=fclose(f);
    if(bad || io || closed) {
        DPRINTF("LUNA PSXCore development config invalid\n");return;
    }
    if(automatic && !autoLaunch)return;
    const char *error=lunaPsxPrepare(bootstrap,count,arguments);
    if(error){DPRINTF("LUNA PSXCore handoff rejected=%s library=retained\n",error);return;}
    lunaPsxExecute();
}
#endif
