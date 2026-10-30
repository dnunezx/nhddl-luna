// One card-management interface; console-specific formats stay in vmc_storage.
static TargetList *vmcLibrary;
void uiVmcSetLibrary(TargetList *library) { vmcLibrary=library; }
typedef struct {
  Target *target; ArgumentList *arguments; LunaGameOptions *options; int slot;
#ifdef LUNA_ENABLE_PSXCORE
  LunaPsxVmcSettings *ps1;
#endif
} VmcAssignment;
static void vmcProgress(unsigned percent) {
  static unsigned previous=101;
  if(percent==previous)return;
  previous=percent;
  char message[96];snprintf(message,sizeof(message),"%s %u%%",lunaText("Verifying memory card..."),percent);ps1Busy(message);
}
static const char *vmcPs2Path(Target *target,const char *value,char out[LUNA_VMC_PATH]) {
  char input[LUNA_VMC_PATH];int n;
  if(strchr(value,':'))n=snprintf(input,sizeof(input),"%s",value);
  else {
    const char *root=storageVMCRoot(target->device);
    if(!root && target->device)root=target->device->mountpoint;
    if(!root)return "Cannot resolve the card drive.";
    size_t length=strlen(root);while(length && root[length-1]=='/')--length;
    while(*value=='/')++value;
    n=snprintf(input,sizeof(input),"%.*s/%s",(int)length,root,value);
  }
  if(n<0 || (size_t)n>=sizeof(input))return "The card path is too long.";
  unsigned used=0;
  for(unsigned i=0;input[i];++i) {
    char c=input[i]=='\\'?'/':input[i];
    if(c=='/' && used && out[used-1]=='/')continue;
    out[used++]=c;
  }
  out[used]=0;
  return NULL;
}
const char *uiVmcCheckPs2(Target *target,ArgumentList *arguments) {
  if(!target || !arguments)return "Cannot read game settings.";
  char paths[2][LUNA_VMC_PATH]={{0}};
  for(unsigned i=0;i<2;++i) {
    Argument *card=getArgument(arguments,i?"mc1":"mc0");
    if(!card || card->isDisabled || !card->value || !card->value[0] || !strcmp(card->value,"no") || !strcmp(card->value,"yes"))continue;
    const char *pathError=vmcPs2Path(target,card->value,paths[i]);if(pathError)return pathError;
    const char *error=lunaVmcReady(paths[i],LUNA_VMC_PS2);if(error)return error;
  }
  if(paths[0][0] && paths[1][0] && !strcasecmp(paths[0],paths[1]))return "Use a different card image in each slot.";
  return NULL;
}
static int vmcEnsureDirectory(const char *path) {
  iox_stat_t st;if(fileXioGetStat(path,&st)>=0)return FIO_S_ISDIR(st.mode);
  return fileXioMkdir(path,0777)>=0;
}
static int vmcStorageDirectory(int platform,const char *root,char path[256]) {
  if(platform==LUNA_VMC_PS1) {
    snprintf(path,256,"%s/POPS",root);if(!vmcEnsureDirectory(path))return 0;
    snprintf(path,256,"%s/POPS/CARDS",root);
  } else snprintf(path,256,"%s%sVMC",root,root[strlen(root)-1]=='/'?"":"/");
  return vmcEnsureDirectory(path);
}
static int vmcCollectRoots(int platform,BrowserRoot *roots) {
#ifndef LUNA_ENABLE_PSXCORE
  (void)platform;
#endif
  int count=0;
  for(int i=0;i<MAX_DEVICES;++i) {
    const char *path=NULL;
#ifdef LUNA_ENABLE_PSXCORE
    char ps1Root[8];
    if(platform==LUNA_VMC_PS1) {if(lunaPsxVmcDevice(&deviceModeMap[i],ps1Root))path=ps1Root;}
    else
#endif
      path=storageVMCRoot(&deviceModeMap[i]);
    if(path)count=addRoot(roots,count,path,path);
  }
  return count;
}
#ifdef LUNA_ENABLE_PSXCORE
static int vmcChooseRoot(int platform,char root[256]) {
  BrowserRoot *roots=calloc(BROWSER_MAX_ROOTS,sizeof(*roots));if(!roots)return 0;
  int count=vmcCollectRoots(platform,roots);
  int selected=0,result=0;BrowserSelector selector={.selected=-1};
  for(;;) {
    drawBrowserFrameSelected(lunaText("Virtual Memory Cards"),lunaText(platform==LUNA_VMC_PS1?"PlayStation":"PlayStation 2"),
      lunaText(count?"Choose a drive.":"No supported local drives found."),
      PROMPT_TWO(ICON_CROSS,lunaText("Select"),ICON_TRIANGLE,lunaText("Back")),count,selected,browserFirstRow(selected),rootRow,roots,&selector);
    int input=readInput();if(input&(PAD_CIRCLE|PAD_TRIANGLE))break;
    if(count && (input&PAD_UP))selected=(selected+count-1)%count;
    else if(count && (input&PAD_DOWN))selected=(selected+1)%count;
    else if(count && (input&PAD_CROSS)){snprintf(root,256,"%s",roots[selected].path);result=1;break;}
  }
  free(roots);return result;
}
#endif
static int vmcCreateNamed(int platform,const char *directory,char path[256]) {
  char name[64];snprintf(name,sizeof(name),"%s",platform==LUNA_VMC_PS1?"New PS1 Card.vmc":"New PS2 Card.bin");
  if(!editFileName(lunaText("Card name"),name,sizeof(name),sizeof(name)-1))return 0;
  if(!name[0] || strchr(name,'/') || strchr(name,'\\') || strchr(name,':') || !strcmp(name,".") || !strcmp(name,".."))return 0;
  if(!joinPath(path,256,directory,name)){ps1Message("The card path is too long.");return 0;}
#ifdef LUNA_ENABLE_PSXCORE
  if(platform==LUNA_VMC_PS1 && !np_file_card_path_valid(path+6)) {
    ps1Message("PS1 card names must end in .vmc inside /POPS/CARDS.");return 0;
  }
#endif
  if(!ps1Confirm("Create card?",platform==LUNA_VMC_PS1?"Create a formatted 128 KB card?":"Create a formatted 8 MB card?"))return 0;
  ps1Busy("Creating memory card...");const char *error=lunaVmcCreate(path,platform,vmcProgress);
  if(error)ps1Message(error);
  return error==NULL;
}
static int vmcAssignSelected(const char *path,void *context) {
  VmcAssignment *a=context;
  const char *error=lunaVmcReady(path,a->target->platform==TARGET_PS1?LUNA_VMC_PS1:LUNA_VMC_PS2);
  if(error){ps1Message(error);return 0;}
#ifdef LUNA_ENABLE_PSXCORE
  if(a->ps1) {
    char root[8],cards[2][NP_FILE_PATH_BYTES],selectedRoot[8];unsigned char volume[16],gameVolume[16];
    if(a->ps1->mode==LUNA_PS1_CARDS_DEFAULT){error=lunaPsxVmcPrepare(a->ps1,1);if(error){ps1Message(error);return 0;}}
    const char *colon=strchr(path,':');
    if(!colon || colon-path!=5 || !np_file_card_path_valid(colon+1)){ps1Message("Choose a PS1 card inside /POPS/CARDS on a supported drive.");return 0;}
    snprintf(selectedRoot,sizeof(selectedRoot),"%.6s",path);
    error=lunaPsxVmcResolve(a->ps1,root,cards);if(error){ps1Message(error);return 0;}
    if(strcmp(root,selectedRoot)){ps1Message("Both PS1 slots must use the same drive. Choose a shared pair first to change drives.");return 0;}
    error=lunaPsxVmcVolume(root,volume,1);if(!error)error=lunaPsxVmcVolume(a->ps1->gameRoot,gameVolume,1);
    if(error){ps1Message(error);return 0;}
    memset(cards[a->slot],0,sizeof(cards[a->slot]));
    snprintf(cards[a->slot],sizeof(cards[a->slot]),"%s",colon+1);
    if(!np_file_card_pair_valid(cards)){ps1Message("Use a different card image in each slot.");return 0;}
    LunaPsxVmcSettings next=*a->ps1;next.mode=LUNA_PS1_CARDS_SLOTS;memset(next.key,0,sizeof(next.key));
    memcpy(next.volume,volume,16);memcpy(next.cards,cards,sizeof(cards));
    error=lunaPsxVmcPrepare(&next,0);if(error){ps1Message(error);return 0;}
    *a->ps1=next;return 1;
  }
#endif
  const char *root=storageVMCRoot(a->target->device);
  if(!root){ps1Message("File cards need enabled local storage.");return 0;}
  size_t n=strlen(root);while(n && root[n-1]=='/')--n;
  if(strncmp(path,root,n) || path[n]!='/'){ps1Message("Choose a card on this game's drive.");return 0;}
  Argument *other=getArgument(a->arguments,a->slot?"mc0":"mc1");char otherPath[LUNA_VMC_PATH];
  if(other && !other->isDisabled && other->value && other->value[0] &&
      !vmcPs2Path(a->target,other->value,otherPath) && !strcasecmp(otherPath,path)) {
    ps1Message("Use a different card image in each slot.");return 0;
  }
  if(!lunaGameOptionsSetVMC(a->options,a->arguments,a->slot,path)){ps1Message("Could not assign the card. Use a different image in each slot.");return 0;}
  return 1;
}
static int vmcGameRow(int index,char *name,size_t nameSize,char *detail,size_t detailSize,void *context) {
  Target **games=context;snprintf(name,nameSize,"%s",games[index]->name);detail[0]=0;(void)detailSize;return 1;
}
static int vmcAssignToGame(int platform,const char *path) {
  if(!vmcLibrary){ps1Message("Open a game to assign this card.");return 0;}
  Target **games=calloc(vmcLibrary->total?vmcLibrary->total:1,sizeof(*games));if(!games)return 0;
  int count=0;for(Target *t=vmcLibrary->first;t;t=t->next)
    if(t->platform==(platform==LUNA_VMC_PS1?TARGET_PS1:TARGET_PS2))games[count++]=t;
  int selected=0;Target *target=NULL;BrowserSelector selector={.selected=-1};
  for(;;) {
    drawBrowserFrameSelected(lunaText("Assign"),path,lunaText(count?"Choose a game.":"No games for this console."),
      PROMPT_TWO(ICON_CROSS,lunaText("Select"),ICON_TRIANGLE,lunaText("Back")),count,selected,browserFirstRow(selected),vmcGameRow,games,&selector);
    int input=readInput();if(input&(PAD_CIRCLE|PAD_TRIANGLE))break;
    if(count && (input&PAD_UP))selected=(selected+count-1)%count;
    else if(count && (input&PAD_DOWN))selected=(selected+1)%count;
    else if(count && (input&PAD_CROSS)){target=games[selected];break;}
  }
  free(games);if(!target)return 0;
  static const char *const slots[]={"Slot 1","Slot 2"};int slot=ps1Action("Assign",target->name,"Choose a slot.",slots,2,0);if(slot<0)return 0;
  VmcAssignment assignment={.target=target,.slot=slot};int changed=0;
#ifdef LUNA_ENABLE_PSXCORE
  if(platform==LUNA_VMC_PS1) {
    LunaPsxVmcSettings settings;const char *error=uiPs1VmcLoad(target,&settings);if(error){ps1Message(error);return 0;}
    assignment.ps1=&settings;changed=vmcAssignSelected(path,&assignment);
    if(changed){error=lunaPsxVmcSave(&settings);if(error){ps1Message(error);return 0;}}
  } else
#endif
  {
    ArgumentList *checked=calloc(1,sizeof(*checked));if(!checked)return 0;
    int readError=getTitleLaunchArguments(checked,target);freeArgumentList(checked);
    if(readError){ps1Message("Cannot read game settings. Existing assignments were preserved.");return 0;}
    ArgumentList *arguments=loadLaunchArgumentLists(target);if(!arguments){ps1Message("Cannot read game settings.");return 0;}
    LunaGameOptions options;lunaGameOptionsRead(&options,arguments);assignment.arguments=arguments;assignment.options=&options;
    changed=vmcAssignSelected(path,&assignment);
    if(changed && updateTitleLaunchArguments(target,arguments)){ps1Message("Could not save the card assignment.");changed=0;}
    freeArgumentList(arguments);
  }
  if(changed)ps1Message("Card selection updated.");
  return changed;
}
static int vmcCardMenu(int platform,const char *path,VmcAssignCard assign,void *context) {
  static const char *const actions[]={"Create","Assign","Duplicate","Import","Export","Back Up","Restore","Recover"};
  static const char *const candidates[]={"Current card","Staged replacement","Previous card","Recovery stage","Previous PS1 save","PS1 save candidate","PS1 staged save","PS1 recovery copy","Earlier PS1 recovery","PS1 pair recovery","Create blank missing card"};
  int action=0;
  for(;;) {
    const char *cardError=lunaVmcReady(path,platform);
    action=ps1Action("Virtual Memory Cards",path,cardError?cardError:
        "Replacing a shared card affects every game using it. Replacements are backed up first.",actions,8,browserPageSection?action:0);
    if(action<0)return 0;
    const char *error=NULL;char source[256],folder[256],destination[256],backup[LUNA_VMC_PATH];
    if(action==1) {if(assign){if(assign(path,context))return 1;}else vmcAssignToGame(platform,path);continue;}
    if(action==0 || action==2) {
      if(strlen(path)>=sizeof(folder))continue;
      snprintf(folder,sizeof(folder),"%s",path);
      char *slash=strrchr(folder,'/');if(!slash)continue;*slash=0;
      if(action==0) {
        if(platform==LUNA_VMC_PS1){snprintf(folder,sizeof(folder),"%.6s/POPS/CARDS",path);if(!vmcEnsureDirectory(folder)){ps1Message("Cannot create the card folder.");continue;}}
        if(vmcCreateNamed(platform,folder,destination) && assign && ps1Confirm("Assign new card?","Use the new card in this slot?") && assign(destination,context))return 1;
        continue;
      }
      char name[64];snprintf(name,sizeof(name),"%s",platform==LUNA_VMC_PS1?"Card Copy.vmc":"Card Copy.bin");
      if(!editFileName(lunaText("Duplicate filename"),name,sizeof(name),sizeof(name)-1))continue;
      if(platform==LUNA_VMC_PS1)snprintf(folder,sizeof(folder),"%.6s/POPS/CARDS",path);
      if(!joinPath(destination,sizeof(destination),folder,name)){ps1Message("The destination path is too long.");continue;}
      ps1Busy("Duplicating and verifying the card...");error=lunaVmcCopy(path,destination,platform,vmcProgress);
    } else if(action==3 || action==6) {
      if(!ps1PickPath(0,action==3?"Import":"Restore",source))continue;
      if(!ps1Confirm("Replace this card?","Every game using this card will see the replacement. The current card will be backed up first."))continue;
      ps1Busy("Backing up and verifying the replacement...");error=lunaVmcReplace(path,source,platform,vmcProgress);
    } else if(action==4) {
      if(!ps1PickPath(1,"Export",folder))continue;
      char name[64];const char *slash=strrchr(path,'/');snprintf(name,sizeof(name),"%.63s",slash?slash+1:path);
      if(!editFileName(lunaText("Export filename"),name,sizeof(name),sizeof(name)-1))continue;
      if(!joinPath(destination,sizeof(destination),folder,name)){ps1Message("The export path is too long.");continue;}
      ps1Busy("Exporting and verifying the card...");error=lunaVmcCopy(path,destination,platform,vmcProgress);
    } else if(action==5) {
      ps1Busy("Backing up and verifying the card...");error=lunaVmcBackup(path,platform,backup,vmcProgress);
    } else {
      const char *ps2Candidates[]={candidates[0],candidates[1],candidates[2],candidates[3],candidates[10]};
      int image=ps1Action("Recover",path,"Choose the candidate to retain. Every candidate is backed up first.",platform==LUNA_VMC_PS1?candidates:ps2Candidates,platform==LUNA_VMC_PS1?11:5,0);if(image<0)continue;
      int blank=platform==LUNA_VMC_PS1?image==10:image==4;
#ifdef LUNA_ENABLE_PSXCORE
      if(image==9) {
        snprintf(folder,sizeof(folder),"%s",path);char *slash=strrchr(folder,'/');if(slash)*slash=0;
        ps1Recover(folder);continue;
      }
#endif
      if(!ps1Confirm("Recover this card?","All candidates will be copied and verified before restoring your choice."))continue;
      ps1Busy("Backing up candidates and recovering the card...");error=lunaVmcRecover(path,blank?".blank":lunaVmcRecoverySuffixes[image],platform,vmcProgress);
    }
    if(error)ps1Message(error);
    else if(action==5)ps1Action("Backup verified",backup,"Restore accepts this backup image.",(const char *const[]){"Back"},1,0);
    else ps1Message("Memory card operation completed and verified.");
  }
}
typedef struct {BrowserEntry *entries;int defaults;} VmcBrowserRows;
static int vmcBrowserRow(int index,char *name,size_t nameSize,char *detail,size_t detailSize,void *context) {
  if(!index){snprintf(name,nameSize,"%s",lunaText("Create"));detail[0]=0;return 1;}
  VmcBrowserRows *rows=context;
  if(rows->defaults && index==1){snprintf(name,nameSize,"%s",lunaText("Default shared cards"));snprintf(detail,detailSize,"2 x 128 KB");return 1;}
  return fileRow(index-1-rows->defaults,name,nameSize,detail,detailSize,rows->entries);
}
static int vmcBrowse(int platform,const char *root,VmcAssignCard assign,void *context) {
  BrowserEntry *entries=calloc(BROWSER_MAX_ENTRIES,sizeof(*entries));if(!entries)return 0;
  char directory[256],boundary[256];if(!vmcStorageDirectory(platform,root,directory)){free(entries);ps1Message("Cannot open the memory card folder.");return 0;}
  snprintf(boundary,sizeof(boundary),"%s",directory);
  int count=0,truncated=0,selected=0,changed=0;BrowserSelector selector={.selected=-1};VmcBrowserRows rows={entries,platform==LUNA_VMC_PS1};
  loadDirectory(directory,entries,&count,&truncated);
  for(;;) {
    drawBrowserFrameSelected(lunaText("Virtual Memory Cards"),directory,lunaText(truncated?"Only the first 512 entries are shown.":"Open a card to manage or assign it."),
      PROMPT_TWO(ICON_CROSS,lunaText("Open/Create"),ICON_TRIANGLE,lunaText("Back")),count+1+rows.defaults,selected,browserFirstRow(selected),vmcBrowserRow,&rows,&selector);
    int total=count+1+rows.defaults;
    int input=readInput();if(input&(PAD_TRIANGLE|PAD_CIRCLE)) {
      char defaults[256];snprintf(defaults,sizeof(defaults),"%s/POPS/ps1emu",root);
      if(platform==LUNA_VMC_PS1 && !strcmp(directory,defaults))snprintf(directory,sizeof(directory),"%s",boundary);
      else {char *slash=strrchr(directory,'/');if(!strcmp(directory,boundary) || !slash)break;*slash=0;}
    } else if(input&PAD_UP){selected=(selected+total-1)%total;continue;}
    else if(input&PAD_DOWN){selected=(selected+1)%total;continue;}
    else if(input&PAD_CROSS) {
      char path[256];
      if(!selected) {
        const char *createAt=platform==LUNA_VMC_PS1?boundary:directory;
        if(vmcCreateNamed(platform,createAt,path) && assign && ps1Confirm("Assign new card?","Use this card in the selected slot?"))changed=assign(path,context);
      } else if(rows.defaults && selected==1) {
        snprintf(directory,sizeof(directory),"%s/POPS/ps1emu",root);
      } else if(joinPath(path,sizeof(path),directory,entries[selected-1-rows.defaults].name)) {
        if(entries[selected-1-rows.defaults].isDirectory)snprintf(directory,sizeof(directory),"%s",path);
        else {
          for(unsigned i=1;i<4;++i){size_t n=strlen(path),suffix=strlen(lunaVmcRecoverySuffixes[i]);if(n>suffix && !strcmp(path+n-suffix,lunaVmcRecoverySuffixes[i])){path[n-suffix]=0;break;}}
          changed=vmcCardMenu(platform,path,assign,context);
        }
      }
      if(changed)break;
    } else continue;
    clearEntries(entries,count);count=0;loadDirectory(directory,entries,&count,&truncated);
    rows.defaults=platform==LUNA_VMC_PS1 && !strcmp(directory,boundary);selected=0;selector.selected=-1;
  }
  clearEntries(entries,count);free(entries);return changed;
}
typedef struct {
  BrowserRoot *roots;
  int selected,count;
  LunaCubePageFocus focus;
} VmcEditor;
static void vmcSubmenuBegin(void *context) {
  VmcEditor *editor=context;
  editor->selected=0;
  editor->focus=(LunaCubePageFocus){.selected=-1,.glideMs=110};
#ifdef LUNA_ENABLE_PSXCORE
  editor->count=2;
#else
  editor->roots=calloc(BROWSER_MAX_ROOTS,sizeof(*editor->roots));
  editor->count=editor->roots?vmcCollectRoots(LUNA_VMC_PS2,editor->roots):0;
#endif
}
static void vmcSubmenuEnd(void *context) {
  VmcEditor *editor=context;
  free(editor->roots);editor->roots=NULL;editor->count=0;
}
static const char *vmcSubmenuLabel(void *context,int row) {
  VmcEditor *editor=context;
#ifdef LUNA_ENABLE_PSXCORE
  (void)editor;return row?"PlayStation 2":"PlayStation";
#else
  return editor->roots[row].label;
#endif
}
static void vmcSubmenuDraw(void *context,float opacity) {
  VmcEditor *editor=context;
#ifdef LUNA_ENABLE_PSXCORE
  const char *status="Choose a console.";
#else
  const char *status=editor->count?"Choose a drive.":"No supported local drives found.";
#endif
  LunaCubePage page={.title="Virtual Memory Cards",.count=editor->count,
      .label=vmcSubmenuLabel,.context=editor,.status=status};
  uiCubeSubmenuPageDraw(&page,editor->selected,&editor->focus,opacity);
}
static int vmcSubmenuInput(void *context,int input) {
  VmcEditor *editor=context;
  if(input&(PAD_CIRCLE|PAD_TRIANGLE))return LUNA_CUBE_SUBMENU_BACK;
  if(!editor->count)return LUNA_CUBE_SUBMENU_STAY;
  if(input&PAD_UP)editor->selected=(editor->selected+editor->count-1)%editor->count;
  else if(input&PAD_DOWN)editor->selected=(editor->selected+1)%editor->count;
  else if(input&PAD_CROSS) {
#ifdef LUNA_ENABLE_PSXCORE
    int platform=editor->selected?LUNA_VMC_PS2:LUNA_VMC_PS1;
    char root[256];while(vmcChooseRoot(platform,root))vmcBrowse(platform,root,NULL,NULL);
#else
    vmcBrowse(LUNA_VMC_PS2,editor->roots[editor->selected].path,NULL,NULL);
#endif
    editor->focus.selected=-1;
  }
  return LUNA_CUBE_SUBMENU_STAY;
}
static int uiVmcPs2SlotPage(Target *target,ArgumentList *arguments,LunaGameOptions *options,int slot,int *selected) {
  VmcAssignment assignment={.target=target,.arguments=arguments,.options=options,.slot=slot};
  static const char *const actions[]={"Choose existing cards","Create","Manage card","Physical card"};
  int action=*selected=ps1Action("Virtual Memory Cards",target->name,"Choose an action for this slot.",actions,4,*selected);if(action<0)return 0;
  if(action==3)return lunaGameOptionsSetVMC(options,arguments,slot,"");
  const char *root=storageVMCRoot(target->device);if(!root){ps1Message("File cards need enabled local storage.");return 0;}
  if(action==0)return vmcBrowse(LUNA_VMC_PS2,root,vmcAssignSelected,&assignment);
  char directory[256],path[LUNA_VMC_PATH];
  if(action==1) {
    if(!vmcStorageDirectory(LUNA_VMC_PS2,root,directory))return 0;
    return vmcCreateNamed(LUNA_VMC_PS2,directory,path) && vmcAssignSelected(path,&assignment);
  }
  Argument *card=getArgument(arguments,slot?"mc1":"mc0");
  if(!card || card->isDisabled || !card->value || !card->value[0]){ps1Message("Choose a virtual card first.");return 0;}
  const char *error=vmcPs2Path(target,card->value,path);if(error){ps1Message(error);return 0;}
  return vmcCardMenu(LUNA_VMC_PS2,path,vmcAssignSelected,&assignment);
}
int uiVmcPs2Slot(Target *target,ArgumentList *arguments,LunaGameOptions *options,int slot) {
  int selected=0;
  do {
    int changed=uiVmcPs2SlotPage(target,arguments,options,slot,&selected);
    if(changed || selected<0)return changed;
  } while(browserPageSection);
  return 0;
}
static int uiVmcPs2PolicyPage(Target *target,ArgumentList *arguments,LunaGameOptions *options,int *selected) {
  static const char *const choices[]={"Create cards for this game","Choose existing cards","Use shared cards"};
  int choice=*selected=ps1Action("Card assignment",target->name,"Choose how this game uses memory cards.",choices,3,*selected);if(choice<0)return 0;
  if(choice==1) {
    static const char *const slots[]={"Slot 1","Slot 2"};int slot=0;
    do {
      slot=ps1Action("Choose existing cards",target->name,"Choose a slot.",slots,2,slot);
      if(slot<0)return 0;
      if(uiVmcPs2Slot(target,arguments,options,slot))return 1;
    } while(browserPageSection);
    return 0;
  }
  const char *root=storageVMCRoot(target->device);if(!root){ps1Message("File cards need enabled local storage.");return 0;}
  char directory[256],paths[2][256];if(!vmcStorageDirectory(LUNA_VMC_PS2,root,directory))return 0;
  if(!ps1Confirm(choice?"Use shared cards?":"Create cards for this game?","This changes both slot assignments. Existing card images will be preserved."))return 0;
  char prefix[49]="Shared";
  if(!choice) {snprintf(prefix,sizeof(prefix),"%.48s",target->id && target->id[0]?target->id:"Game");if(!editFileName(lunaText("Card pair name"),prefix,sizeof(prefix),48))return 0;}
  if(!prefix[0] || strchr(prefix,'/') || strchr(prefix,'\\') || strchr(prefix,':'))return 0;
  for(unsigned i=0;i<2;++i) {
    char name[64];snprintf(name,sizeof(name),"%s_slot%u.bin",prefix,i+1);
    if(!joinPath(paths[i],sizeof(paths[i]),directory,name)){ps1Message("The card path is too long.");return 0;}
    iox_stat_t st;int exists=fileXioGetStat(paths[i],&st)>=0;
    if(!choice && exists){ps1Message("A card with this name already exists. Choose existing cards or another name.");return 0;}
  }
  for(unsigned i=0;i<2;++i) {
    iox_stat_t st;const char *error=fileXioGetStat(paths[i],&st)>=0?lunaVmcReady(paths[i],LUNA_VMC_PS2):lunaVmcCreate(paths[i],LUNA_VMC_PS2,vmcProgress);
    if(error){ps1Message(error);return 0;}
  }
  return lunaGameOptionsSetVMCPair(options,arguments,paths[0],paths[1]);
}
int uiVmcPs2Policy(Target *target,ArgumentList *arguments,LunaGameOptions *options) {
  int selected=0;
  do {
    int changed=uiVmcPs2PolicyPage(target,arguments,options,&selected);
    if(changed || selected<0)return changed;
  } while(browserPageSection);
  return 0;
}
#ifdef LUNA_ENABLE_PSXCORE
static int uiPs1VmcSlotPage(Target *target,LunaPsxVmcSettings *settings,int slot,int *selected) {
  static const char *const choices[]={"Choose existing cards","Create","Manage card"};
  int choice=*selected=ps1Action("Virtual Memory Cards",target->name,"Both slots use one save drive. Each slot can use a different card.",choices,3,*selected);if(choice<0)return 0;
  VmcAssignment assignment={.target=target,.slot=slot,.ps1=settings};
  char root[8],cards[2][NP_FILE_PATH_BYTES],directory[256],path[256];const char *error=lunaPsxVmcResolve(settings,root,cards);
  if(error){ps1Message(error);return 0;}
  if(choice==0)return vmcBrowse(LUNA_VMC_PS1,root,vmcAssignSelected,&assignment);
  if(choice==1) {
    if(!vmcStorageDirectory(LUNA_VMC_PS1,root,directory))return 0;
    return vmcCreateNamed(LUNA_VMC_PS1,directory,path) && vmcAssignSelected(path,&assignment);
  }
  snprintf(path,sizeof(path),"%s%s",root,cards[slot]);return vmcCardMenu(LUNA_VMC_PS1,path,vmcAssignSelected,&assignment);
}
int uiPs1VmcSlot(Target *target,LunaPsxVmcSettings *settings,int slot) {
  int selected=0;
  do {
    int changed=uiPs1VmcSlotPage(target,settings,slot,&selected);
    if(changed || selected<0)return changed;
  } while(browserPageSection);
  return 0;
}
static int uiPs1VmcPolicyPage(Target *target,LunaPsxVmcSettings *settings,int *selected) {
  static const char *const choices[]={"Create cards for this game","Choose existing cards","Use shared cards"};
  int choice=*selected=ps1Action("Card assignment",target->name,"All discs in this game use the same slot assignments.",choices,3,*selected);if(choice<0)return 0;
  if(choice==1) {
    static const char *const actions[]={"Slot 1","Slot 2","Named pair presets"};
    int action=0;
    do {
      action=ps1Action("Choose existing cards",target->name,"Choose a slot or a pair preset.",actions,3,action);
      if(action<0)return 0;
      if(action==2?uiPs1VmcPicker(target,settings):uiPs1VmcSlot(target,settings,action))return 1;
    } while(browserPageSection);
    return 0;
  }
  if(choice==2) {
    static const char *const actions[]={"Default shared cards","Named pair presets"};
    int action=0;
    do {
      action=ps1Action("Use shared cards",target->name,"Games selecting the same cards share saved data.",actions,2,action);
      if(action<0)return 0;
      if(action==1) { if(uiPs1VmcPicker(target,settings))return 1; }
      else if(ps1Confirm("Use default shared cards?","Both slots will use the default shared pair. Existing images are preserved.")) {
        settings->mode=LUNA_PS1_CARDS_DEFAULT;memset(settings->key,0,sizeof(settings->key));memset(settings->volume,0,16);memset(settings->cards,0,sizeof(settings->cards));return 1;
      }
    } while(browserPageSection);
    return 0;
  }
  char name[49];snprintf(name,sizeof(name),"%.48s",target->id && target->id[0]?target->id:"Game Cards");
  for(unsigned i=0;name[i];++i)
    if(!isalnum((unsigned char)name[i]) && name[i]!=' ' && name[i]!='_' && name[i]!='-')name[i]='_';
  if(!editFileName(lunaText("Card pair name"),name,sizeof(name),48))return 0;
  if(!ps1Confirm("Create cards for this game?","Create two new cards and assign them to every disc in this game? Existing cards are preserved."))return 0;
  LunaPsxVmcSettings next=*settings;char key[49];ps1Busy("Creating and verifying memory cards...");
  const char *error=lunaPsxVmcCreate(settings->gameRoot,name,key);if(!error)error=lunaPsxVmcVolume(settings->gameRoot,next.volume,0);
  if(error){ps1Message(error);return 0;}
  next.mode=LUNA_PS1_CARDS_CUSTOM;memset(next.cards,0,sizeof(next.cards));snprintf(next.key,sizeof(next.key),"%s",key);*settings=next;return 1;
}
int uiPs1VmcPolicy(Target *target,LunaPsxVmcSettings *settings) {
  int selected=0;
  do {
    int changed=uiPs1VmcPolicyPage(target,settings,&selected);
    if(changed || selected<0)return changed;
  } while(browserPageSection);
  return 0;
}
#endif
