// Original LUNA selected PS1 game preparation, 2026.
#include "luna_psxcore.h"
#include "luna_psxcore_library.h"
#include "devices/devices.h"
#include "dprintf.h"
#include "psxcore_hash.h"
#include "psxcore_file.h"
#include "options.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

// FATFS reports both a missing file (-2) and a missing parent path (-4).
// Keep the IOP result: newlib's errno translation obscures that distinction.
static int titleStat(const char *path, uint64_t *bytes, int directory) {
    iox_stat_t st;
    int result=fileXioGetStat(path,&st);
    if(result==-2 || result==-4)return 1;
    if(result<0) {
        DPRINTF("LUNA PS1 stat failed path=%s result=%d\n",path,result);
        return -1;
    }
    if(directory?!FIO_S_ISDIR(st.mode):!FIO_S_ISREG(st.mode))return -1;
    if(bytes)*bytes=((uint64_t)st.hisize<<32)|st.size;
    return 0;
}

static int readable(const char *path) {
    FILE *f=fopen(path,"rb");if(!f)return 0;
    int byte=fgetc(f),bad=ferror(f),closed=fclose(f);
    return byte!=EOF && !bad && !closed;
}
static int titleError(char *out,size_t size,const char *message) {
    snprintf(out,size,"%s",message);return -1;
}
int lunaPsxPrepareTitle(Target *target,int createCards,char *error,size_t size,
                        int (*progress)(uint64_t completed,uint64_t total)) {
    if(!target || target->platform!=TARGET_PS1 || !target->device ||
       target->device->mode!=MODE_ATA || target->device->index!=0)
        return titleError(error,size,"PS1 games require the internal ATA drive.");
    const char *path=target->fullPath;
    if(!path || strlen(path)<8 || strncmp(path,"mass",4) || path[4]<'0' || path[4]>'9' ||
       path[5]!=':' || strlen(path+6)>=NP_FILE_PATH_BYTES ||
       !np_file_path_valid(path+6,NP_FILE_PATH_BYTES,0))
        return titleError(error,size,"The PS1 game path is unsupported or too long.");
    char id[12];const char *reason=lunaPsxDiscInfo(path,id);
    if(reason){snprintf(error,size,"Cannot prepare PS1 disc: %s",reason);return -1;}
    char asset[192];
    const char *names[]={"POPS.ELF","IOPRP252.IMG"};
    for(unsigned i=0;i<2;++i) {
        snprintf(asset,sizeof(asset),"%.6s/POPS/%s",path,names[i]);
        if(!readable(asset)){snprintf(error,size,"Missing or unreadable firmware: %s",asset);return -1;}
    }
    char base[96],cardPath[128];
    int missing=0,present=0;
    snprintf(base,sizeof(base),"%.6s/POPS/SAVES",path);
    int status=titleStat(base,NULL,1);
    if(status<0)return titleError(error,size,"Cannot read the PS1 save directory.");
    // A new save library needs no full-disc read merely to ask for permission.
    // After confirmation PSXCore performs all identity and save validation.
    if(status==1 || createCards) {missing=1;goto launch;}
    // Read-only preflight: PSXCore remains responsible for ownership, creation,
    // reopening and saving. Its direct-file CLI uses this content-based key.
    FILE *disc=fopen(path,"rb");if(!disc)return titleError(error,size,"The selected PS1 game is no longer readable.");
    unsigned char *buffer=malloc(65536),digest[32];NpSha256 hash;
    if(!buffer){fclose(disc);return titleError(error,size,"Not enough memory to prepare this PS1 game.");}
    uint64_t total=0,done=0,nextLog=0;
    if(titleStat(path,&total,0)) {free(buffer);fclose(disc);return titleError(error,size,"Cannot read the PS1 game size.");}
    np_sha256_init(&hash);size_t n;int cancelled=0;
    while((n=fread(buffer,1,65536,disc))>0) {
        np_sha256_update(&hash,buffer,n);
        done+=n;
        if(done>=nextLog) {
            DPRINTF("LUNA PS1 checking saves=%u/%u MiB\n",(unsigned)(done>>20),(unsigned)(total>>20));
            nextLog=done+16u*1024*1024;
        }
        if(progress && progress(done,total)){cancelled=1;break;}
    }
    int bad=ferror(disc),closed=fclose(disc);free(buffer);
    if(cancelled)return titleError(error,size,"PS1 preparation cancelled.");
    if(bad || closed || done!=total)return titleError(error,size,"Could not read the complete PS1 game.");
    np_sha256_final(&hash,digest);
    char key[33],owner[66];
    for(unsigned i=0;i<32;++i)snprintf(owner+i*2,3,"%02x",digest[i]);
    memcpy(key,owner,32);key[32]=0;owner[64]='\n';owner[65]=0;
    snprintf(base,sizeof(base),"%.6s/POPS/SAVES/%s",path,key);
    for(unsigned i=0;i<2;++i) {
        snprintf(cardPath,sizeof(cardPath),"%s/card%u",base,i);
        uint64_t cardBytes=0;status=titleStat(cardPath,&cardBytes,0);
        if(status) {
            if(status<0)return titleError(error,size,"Cannot read this game's PS1 memory cards.");
            missing=1;continue;
        }
        ++present;
        unsigned char head[128];FILE *card=fopen(cardPath,"rb");
        if(!card)return titleError(error,size,"Cannot open this game's PS1 memory cards.");
        n=fread(head,1,sizeof(head),card);bad=ferror(card);closed=fclose(card);
        if(cardBytes!=NP_FILE_CARD_BYTES || n!=sizeof(head) || bad || closed ||
           !np_file_card_header_valid(head,n))
            return titleError(error,size,"A PS1 memory card is invalid. Restore a valid copy before launching.");
        const char *suffix[]={".new",".bak"};
        for(unsigned j=0;j<2;++j) {
            snprintf(cardPath,sizeof(cardPath),"%s/card%u%s",base,i,suffix[j]);
            if(titleStat(cardPath,NULL,0)!=1)
                return titleError(error,size,"An unfinished PS1 save needs recovery before launching.");
        }
    }
    snprintf(cardPath,sizeof(cardPath),"%s/owner.sha256",base);
    status=titleStat(cardPath,NULL,0);
    if(!status) {
        FILE *owned=fopen(cardPath,"rb");
        if(!owned)return titleError(error,size,"Cannot open PS1 save ownership.");
        char actual[66];n=fread(actual,1,sizeof(actual),owned);bad=ferror(owned);closed=fclose(owned);
        if(n!=65 || bad || closed || memcmp(actual,owner,65))
            return titleError(error,size,"These PS1 memory cards belong to a different disc.");
    } else if(present || status<0)
        return titleError(error,size,"PS1 save ownership is missing or unreadable. Preserve the cards before repair.");
launch:
    if(missing && !createCards) {
        snprintf(error,size,"This game needs PS1 memory cards.\nPress Square to create missing cards and launch.\nPress Triangle to return.");
        return 1;
    }
    char bootstrap[PATH_MAX+1];
    const char *slash=strrchr(NEUTRINO_ELF_PATH,'/');
    if(!slash || (size_t)(slash-NEUTRINO_ELF_PATH)+sizeof("/psxcore-runtime-bootstrap.elf")>sizeof(bootstrap))
        return titleError(error,size,"Cannot locate PSXCore beside the LUNA runtime.");
    snprintf(bootstrap,sizeof(bootstrap),"%.*s/psxcore-runtime-bootstrap.elf",(int)(slash-NEUTRINO_ELF_PATH),NEUTRINO_ELF_PATH);
    const char *args[5]={"--file",path};unsigned count=2;
#ifdef LUNA_EMULATOR_BUILD
    args[count++]="--pcsx2";
#endif
    if(missing && createCards)args[count++]="--init-cards";
    reason=lunaPsxPrepare(bootstrap,count,args);
    if(reason){snprintf(error,size,"Cannot prepare PSXCore: %s",reason);return -1;}
    if(updateLastLaunchedTitle(target->device,target->fullPath))
        DPRINTF("WARN: Could not record selected PS1 game\n");
    DPRINTF("LUNA PS1 selected=%s id=%s create-cards=%d\n",path,target->id,missing && createCards);
    return 0;
}
