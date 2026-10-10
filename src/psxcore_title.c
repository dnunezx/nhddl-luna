// Original LUNA selected PS1 game preparation, 2026.
#include "luna_psxcore.h"
#include "luna_psxcore_library.h"
#include "luna_psxcore_vmc.h"
#include "devices/devices.h"
#include "dprintf.h"
#include "psxcore_file.h"
#include "psxcore_shared_store.h"
#include "options.h"
#include <stdio.h>
#include <string.h>

static int readable(const char *path) {
    FILE *f=fopen(path,"rb");if(!f)return 0;
    int byte=fgetc(f),bad=ferror(f),closed=fclose(f);
    return byte!=EOF && !bad && !closed;
}
static int titleError(char *out,size_t size,const char *message) {
    snprintf(out,size,"%s",message);return -1;
}
int lunaPsxPrepareTitleCardsAutomatic(Target *target,const LunaPsxVmcSettings *selection,
    int createCards,char *error,size_t size,
    int (*progress)(uint64_t completed,uint64_t total), LunaPsxVmcChoose choose) {
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
    char asset[192];const char *names[]={"POPS.ELF","IOPRP252.IMG"};
    for(unsigned i=0;i<2;++i) {
        snprintf(asset,sizeof(asset),"%.6s/POPS/%s",path,names[i]);
        if(!readable(asset)){snprintf(error,size,"Missing or unreadable firmware: %s",asset);return -1;}
    }
    LunaPsxVmcSettings settings;
    reason=lunaPsxVmcLoad(target,&settings,progress);
    if(reason)return titleError(error,size,reason);
    if(selection) {
        if(memcmp(selection->disc,settings.disc,32) || strcmp(selection->gameRoot,settings.gameRoot))
            return titleError(error,size,"The selected disc changed. Reopen its memory card settings.");
        settings=*selection;
    }
    reason=lunaPsxVmcAutomatic(target,&settings,1,progress,choose);
    if(reason)return titleError(error,size,reason);
    int missing=0;char saveRoot[8]={0},discId[33]={0},saveId[33]={0},device[4]={0};
    const char *saveBackend=NULL;
    if(settings.key[0]) {
        char base[128];NpSharedSet set;unsigned char volume[16];
        reason=lunaPsxVmcBase(&settings,base);if(reason)return titleError(error,size,reason);
        if(np_shared_store_read(base,&set))return titleError(error,size,"The shared cards are invalid or need recovery.");
        if(np_shared_match(&set,NP_SHARED_FILE,settings.key,settings.volume,settings.disc))
            return titleError(error,size,"This disc is not enrolled in the selected card set. Select it again in settings.");
        reason=lunaPsxVmcVolume(settings.gameRoot,volume,0);if(reason)return titleError(error,size,reason);
        np_file_volume_id_text(volume,discId);np_file_volume_id_text(settings.volume,saveId);
        reason=lunaPsxVmcFindVolume(settings.volume,saveRoot);if(reason)return titleError(error,size,reason);
        for(int i=0;i<MAX_DEVICES;++i) {
            char root[8];
            if(lunaPsxVmcDevice(&deviceModeMap[i],root) && !strcmp(root,saveRoot)) {
                saveBackend=deviceModeMap[i].mode==MODE_USB?"usb":"ata";
                snprintf(device,sizeof(device),"%u",(unsigned)deviceModeMap[i].index);break;
            }
        }
        if(!saveBackend)return titleError(error,size,"The selected card drive is unavailable.");
    } else {
        reason=lunaPsxVmcPrivate(&settings,0);
        if(reason && !strcmp(reason,"Private cards have not been created yet."))missing=1;
        else if(reason)return titleError(error,size,reason);
    }
    if(settings.mode==LUNA_PS1_CARDS_AUTO) createCards=1;
    if(missing && !createCards) {
        snprintf(error,size,"This game needs PS1 memory cards.\nPress Square to create missing cards and launch.\nPress Triangle to return.");return 1;
    }
    char bootstrap[PATH_MAX+1];const char *slash=strrchr(NEUTRINO_ELF_PATH,'/');
    if(!slash || (size_t)(slash-NEUTRINO_ELF_PATH)+sizeof("/psxcore-runtime-bootstrap.elf")>sizeof(bootstrap))
        return titleError(error,size,"Cannot locate PSXCore beside the LUNA runtime.");
    snprintf(bootstrap,sizeof(bootstrap),"%.*s/psxcore-runtime-bootstrap.elf",(int)(slash-NEUTRINO_ELF_PATH),NEUTRINO_ELF_PATH);
    const char *args[20]={"--file",path};unsigned count=2;
#ifdef LUNA_EMULATOR_BUILD
    args[count++]="--pcsx2";
#endif
    if(settings.key[0]) {
        args[count++]="--shared-set";args[count++]=settings.key;
        args[count++]="--volume-id";args[count++]=discId;
        args[count++]="--save-volume";args[count++]=saveRoot;
        args[count++]="--save-volume-id";args[count++]=saveId;
        args[count++]="--save-backend";args[count++]=saveBackend;
        args[count++]="--save-device";args[count++]=device;
    } else if(missing && createCards)args[count++]="--init-cards";
    reason=lunaPsxPrepare(bootstrap,count,args);
    if(reason){snprintf(error,size,"Cannot prepare PSXCore: %s",reason);return -1;}
    if(updateLastLaunchedTitle(target->device,target->fullPath)) {
        DPRINTF("WARN: Could not record selected PS1 game\n");
    }
    DPRINTF("LUNA PS1 selected=%s id=%s cards=%s create-cards=%d\n",path,target->id,settings.key[0]?settings.key:"private",missing && createCards);
    return 0;
}
int lunaPsxPrepareTitleCards(Target *target,const LunaPsxVmcSettings *selection,
    int createCards,char *error,size_t size,
    int (*progress)(uint64_t completed,uint64_t total)) {
    return lunaPsxPrepareTitleCardsAutomatic(target,selection,createCards,error,size,progress,NULL);
}
int lunaPsxPrepareTitle(Target *target,int createCards,char *error,size_t size,
    int (*progress)(uint64_t completed,uint64_t total)) {
    return lunaPsxPrepareTitleCards(target,NULL,createCards,error,size,progress);
}
