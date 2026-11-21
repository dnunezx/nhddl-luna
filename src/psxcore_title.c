// LUNA supplies existing shared cards to the unchanged PSXCore bootstrap.
#include "luna_psxcore.h"
#include "luna_psxcore_library.h"
#include "luna_psxcore_discset.h"
#include "luna_psxcore_vmc.h"
#include "devices/devices.h"
#include "options.h"
#include "storage.h"
#include "psxcore_hash.h"
#include "ui/language.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Match the unchanged bootstrap's firmware profile before leaving the UI.
static const char *firmwareCheck(const char *path,unsigned index) {
    static const unsigned sizes[]={3166988,265233};
    static const unsigned char hashes[2][32]={
        {0x59,0xdf,0x33,0x89,0xc4,0xdf,0x88,0xa5,0x72,0xda,0xa7,0x20,0xb0,0x55,0x07,0xc5,
         0x2c,0x34,0xed,0xdf,0xa0,0x03,0x1a,0x6f,0xbe,0xec,0x55,0xe0,0xc2,0xd0,0xfc,0xb1},
        {0x33,0x38,0xb2,0x38,0xd8,0x4d,0x7d,0x58,0x6b,0x71,0x66,0x77,0xe3,0xa1,0xc0,0x3b,
         0x20,0x88,0xb8,0x82,0xec,0xfa,0x17,0xf9,0x1f,0xc3,0x37,0x98,0x93,0x1c,0xa3,0xba}
    };
    FILE *f=fopen(path,"rb");if(!f)return "Missing or unreadable firmware";
    const char *error=NULL;
    if(fseek(f,0,SEEK_END) || ftell(f)!=(long)sizes[index])error="Unsupported firmware size";
    if(!error && fseek(f,0,SEEK_SET))error="Cannot read firmware";
    NpSha256 sha;unsigned char data[4096],actual[32];np_sha256_init(&sha);
    for(unsigned done=0;!error && done<sizes[index];) {
        unsigned take=sizes[index]-done;if(take>sizeof(data))take=sizeof(data);
        if(fread(data,1,take,f)!=take){error="Cannot read firmware";break;}
        np_sha256_update(&sha,data,take);done+=take;
    }
    if(!error && (fgetc(f)!=EOF || ferror(f)))error="Cannot read firmware";
    if(fclose(f) && !error)error="Cannot read firmware";
    if(!error) {
        np_sha256_final(&sha,actual);
        if(memcmp(actual,hashes[index],sizeof(actual)))error="Firmware does not match PSXCore";
    }
    return error;
}
static int titleError(char *out,size_t size,const char *message) {snprintf(out,size,"%s",lunaText(message));return -1;}
int lunaPsxPrepareTitleCards(Target *target,const LunaPsxVmcSettings *selection,
    int createCards,int codeValidation,int hdtvFix,int startDisc,char *error,size_t size,int (*progress)(uint64_t,uint64_t)) {
    if(!target || target->platform!=TARGET_PS1 || !target->device ||
       !(STORAGE_SETTINGS.enabled & target->device->mode) ||
       !((target->device->mode==MODE_ATA && target->device->index==0) ||
         (target->device->mode==MODE_USB && target->device->index<=1)))
        return titleError(error,size,"PS1 games require an enabled internal ATA or USB drive.");
    if(target->discs && target->discs->error[0])return titleError(error,size,target->discs->error);
    if(codeValidation<0 || hdtvFix<0 || startDisc<0) {
        ArgumentList saved={0};int result=getTitleLaunchArguments(&saved,target);
        Argument *option=getArgument(&saved,"luna_ps1_code_validation");
        if(codeValidation<0)codeValidation=option && !option->isDisabled;
        option=getArgument(&saved,"luna_ps1_hdtv_fix");
        if(hdtvFix<0)hdtvFix=option && !option->isDisabled;
        option=getArgument(&saved,"luna_ps1_start_disc");
        if(startDisc<0) {
            startDisc=0;
            if(option && !option->isDisabled && option->value) {
                if(strlen(option->value)!=1 || option->value[0]<'1' || option->value[0]>'4')result=-1;
                else startDisc=option->value[0]-'1';
            }
        }
        // freeArgumentList owns the list itself, so release stack-list entries here.
        while(saved.first) {
            Argument *next=saved.first->next;
            free(saved.first->arg);free(saved.first->value);free(saved.first);saved.first=next;
        }
        if(result)return titleError(error,size,"Could not read PS1 compatibility settings.");
    }
    char selectedPath[264];
    if(target->discs) {
        if((unsigned)startDisc>=target->discs->count)return titleError(error,size,"The starting disc is unavailable. Choose it again.");
        snprintf(selectedPath,sizeof(selectedPath),"%.6s%s",target->fullPath,target->discs->paths[startDisc]);
    } else snprintf(selectedPath,sizeof(selectedPath),"%s",target->fullPath);
    const char *path=target->discs?selectedPath:target->fullPath;
    LunaPsxVmcSettings settings;const char *reason=lunaPsxVmcLoad(target,&settings,progress);
    if(reason)return titleError(error,size,reason);
    if(selection) {
        if(strcmp(selection->gamePath,settings.gamePath) || strcmp(selection->gameRoot,settings.gameRoot))
            return titleError(error,size,"The selected game changed. Reopen its memory card settings.");
        settings=*selection;
    }
    char id[12];reason=lunaPsxDiscInfo(path,id);
    if(reason){snprintf(error,size,lunaText("Cannot prepare PS1 disc: %s"),lunaText(reason));return -1;}
    char asset[192];const char *names[]={"POPS.ELF","IOPRP252.IMG"};
    for(unsigned i=0;i<2;++i) {
        snprintf(asset,sizeof(asset),"%.6s/POPS/%s",path,names[i]);
        reason=firmwareCheck(asset,i);
        if(reason){snprintf(error,size,"%s: %s",lunaText(reason),asset);return -1;}
    }
    NpFileDiscSet discSet={0};
    if(target->discs) {
        reason=lunaPsxDiscSetMeasure(target,(unsigned)startDisc,&discSet,progress);
        if(reason)return titleError(error,size,reason);
    }
    if(settings.mode==LUNA_PS1_CARDS_DEFAULT)createCards=1;
    reason=lunaPsxVmcPrepare(&settings,createCards);
    if(reason) {
        if(!strcmp(reason,"Memory cards have not been created yet.")) {
            snprintf(error,size,"%s",lunaText("This pair needs PS1 memory cards.\nPress Square to create missing cards and launch.\nPress Triangle to return."));return 1;
        }
        return titleError(error,size,reason);
    }
    char cards[2][NP_FILE_PATH_BYTES],saveRoot[8],discId[33],saveId[33],saveDevice[4],discDevice[4];
    reason=lunaPsxVmcResolve(&settings,saveRoot,cards);if(reason)return titleError(error,size,reason);
    const char *saveBackend=NULL;
    for(int i=0;i<MAX_DEVICES;++i) {
        char root[8];
        if(lunaPsxVmcDevice(&deviceModeMap[i],root) && !strcmp(root,saveRoot)) {
            saveBackend=deviceModeMap[i].mode==MODE_USB?"usb":"ata";
            snprintf(saveDevice,sizeof(saveDevice),"%u",(unsigned)deviceModeMap[i].index);break;
        }
    }
    if(!saveBackend)return titleError(error,size,"The selected card drive is unavailable.");
    int custom=settings.mode!=LUNA_PS1_CARDS_DEFAULT;
    unsigned char gameVolume[16]={0};
    if(custom || target->discs) {
        char verified[8];
        reason=lunaPsxVmcVolume(settings.gameRoot,gameVolume,1);if(reason)return titleError(error,size,reason);
        reason=lunaPsxVmcFindVolume(gameVolume,verified);if(reason)return titleError(error,size,reason);
        np_file_volume_id_text(gameVolume,discId);np_file_volume_id_text(settings.volume,saveId);
    }
    char descriptor[NP_FILE_PATH_BYTES];
    if(target->discs) {
        memcpy(discSet.disc_volume,gameVolume,16);
        np_file_disc_save_binding(cards,!strcmp(saveBackend,"usb")?NP_FILE_BACKEND_USB:NP_FILE_BACKEND_ATA,
            (uint32_t)strtoul(saveDevice,NULL,10),custom?settings.volume:gameVolume,discSet.save_binding);
        reason=np_file_disc_set_check(&discSet,sizeof(discSet),path+6,gameVolume,discSet.save_binding);
        if(!reason)reason=lunaPsxDiscSetPublish(settings.gameRoot,&discSet,descriptor);
        if(reason)return titleError(error,size,reason);
    }
    char bootstrap[PATH_MAX+1];const char *slash=strrchr(NEUTRINO_ELF_PATH,'/');
    if(!slash || (size_t)(slash-NEUTRINO_ELF_PATH)+sizeof("/psxcore-runtime-bootstrap.elf")>sizeof(bootstrap))
        return titleError(error,size,"Cannot locate PSXCore beside the LUNA runtime.");
    snprintf(bootstrap,sizeof(bootstrap),"%.*s/psxcore-runtime-bootstrap.elf",(int)(slash-NEUTRINO_ELF_PATH),NEUTRINO_ELF_PATH);
    snprintf(discDevice,sizeof(discDevice),"%u",(unsigned)target->device->index);
    const char *args[LUNA_PSX_MAX_ARGS]={"--file",path,"--backend",target->device->mode==MODE_USB?"usb":"ata",
        "--device",discDevice,"--card0",cards[0],"--card1",cards[1]};unsigned count=10;
    // The pinned firmware's direct-file boot needs PSXCore's guarded license
    // adapter. Use its public option; the core validates and owns the change.
    args[count++]="--license";
    args[count++]=codeValidation?"--code-validation":"--no-code-validation";
    if(hdtvFix)args[count++]="--hdtv-fix";
    if(target->discs) {args[count++]="--disc-set";args[count++]=descriptor;}
    if(custom || target->discs) {args[count++]="--volume-id";args[count++]=discId;}
    if(custom) {
        args[count++]="--save-volume";args[count++]=saveRoot;
        args[count++]="--save-volume-id";args[count++]=saveId;
        args[count++]="--save-backend";args[count++]=saveBackend;
        args[count++]="--save-device";args[count++]=saveDevice;
    }
    reason=lunaPsxPrepare(bootstrap,count,args);
    if(reason){snprintf(error,size,lunaText("Cannot prepare PSXCore: %s"),lunaText(reason));return -1;}
    (void)updateLastLaunchedTitle(target->device,target->fullPath);
    return 0;
}
