/* Apps use the standard Settings cube hub, with opt-in scrolling. */
typedef struct {
  LunaApps *apps;
  int selected,action,loadError;
  int checked,available;
  char status[160];
} AppsMenu;

static const char *appSaveError(int error) {
  return error==-EEXIST?"This app is already in Apps.":error==-ENOSPC?"The Apps list is full.":
      "Could not save Apps. Check the launcher drive.";
}
static int loadAppShortcuts(LunaApps *apps) {
  int error=lunaAppsLoad(apps);
  if(error || apps->hasConfig)return error;
  BrowserRoot *roots=calloc(BROWSER_MAX_ROOTS,sizeof(*roots));if(!roots)return -ENOMEM;
  int count=collectRoots(roots);
  for(int i=0;i<count;i++) {
    char path[PATH_MAX+1];
    if(joinPath(path,sizeof(path),roots[i].path,"APPS") && lunaAppsScan(apps,path,0)<0){error=-ENOMEM;break;}
    if(!strncmp(roots[i].path,"mc",2) && lunaAppsScan(apps,roots[i].path,1)<0){error=-ENOMEM;break;}
  }
  free(roots);
  if(error)memset(apps,0,sizeof(*apps));
  return error;
}
static void addAppShortcut(const char *path,char *status,size_t size) {
  LunaApps *apps=calloc(1,sizeof(*apps));
  if(!apps){snprintf(status,size,"%s",lunaText("Not enough memory."));return;}
  int error=loadAppShortcuts(apps);
  if(error){snprintf(status,size,"%s",lunaText("Could not read Apps. Check the launcher drive."));free(apps);return;}
  char name[LUNA_APP_NAME+1];lunaAppSuggestName(path,name,sizeof(name));
  if(editFileName(lunaText("App name"),name,sizeof(name),LUNA_APP_NAME)) {
    error=lunaAppsAdd(apps,path,name);
    snprintf(status,size,"%s",lunaText(error?appSaveError(error):"Added to Apps."));
  }
  free(apps);
}
static int appPickerRow(int index,char *name,size_t size,char *detail,size_t detailSize,void *context) {
  BrowserEntry *entry=browserPaneEntry(context,index);if(!entry)return 0;
  snprintf(name,size,"%s",entry->name);snprintf(detail,detailSize,"%s",entry->isDirectory?">":"");return 1;
}
static int appFileMatches(const char *name) {
  size_t length=strlen(name);
  return length>4 && !strcasecmp(name+length-4,".elf");
}
static int pickElfFile(char *path,size_t size,const char *title,int (*rootAllowed)(const char *)) {
  BrowserRoot *roots=calloc(BROWSER_MAX_ROOTS,sizeof(*roots));
  BrowserPane *pane=calloc(1,sizeof(*pane));
  if(!roots || !pane){free(roots);free(pane);return 0;}
  pane->root=-1;int rootCount=collectRoots(roots),previous=pollInput(),result=0;
  if(rootAllowed) {
    int kept=0;
    for(int i=0;i<rootCount;i++)
      if(rootAllowed(roots[i].path))roots[kept++]=roots[i];
    rootCount=kept;
  }
  BrowserSelector selector={.selected=-1};char status[160]="";
  for(;;) {
    if(pane->root>=0) {
      browserPaneStep(pane);pane->visibleCount=0;
      for(int i=0;i<pane->count;i++)if(pane->entries[i].isDirectory || appFileMatches(pane->entries[i].name))
        pane->visible[pane->visibleCount++]=i;
      if(pane->selected>=pane->visibleCount)pane->selected=pane->visibleCount?pane->visibleCount-1:0;
    }
    int total=pane->root<0?rootCount:pane->visibleCount;
    int *selected=pane->root<0?&pane->rootSelected:&pane->selected;
    drawBrowserFrameSelected(lunaText(title),
        pane->root<0?lunaText("Devices"):pane->path,status,
        PROMPT_TWO(ICON_CROSS,lunaText("Select"),ICON_CIRCLE,lunaText("Back")),
        total,*selected,browserFirstRow(*selected),pane->root<0?rootRow:appPickerRow,
        pane->root<0?(void *)roots:(void *)pane,&selector);
    int held=pollInput(),input=held&~previous;previous=held;
    if(input&PAD_TRIANGLE)break;
    if(input&PAD_CIRCLE) {
      if(pane->root<0)break;
      if(!strcmp(pane->path,roots[pane->root].path)){browserPaneDestroy(pane);pane->root=-1;}
      else if(!browserPaneUp(pane,roots[pane->root].path))snprintf(status,sizeof(status),"%s",lunaText("Could not open this folder."));
    } else if((input&(PAD_UP|PAD_DOWN|PAD_L2|PAD_R2)) && total) {
      int step=input&(PAD_L2|PAD_R2)?fileManagerLayout().visible:1;
      *selected+=input&(PAD_UP|PAD_L2)?-step:step;
      if(*selected<0)*selected=0;
      if(*selected>=total)*selected=total-1;
    } else if(input&PAD_CROSS) {
      if(pane->root<0 && total) {
        int root=pane->rootSelected;
        if(browserPaneOpen(pane,roots[root].path)){pane->root=root;status[0]=0;}
        else snprintf(status,sizeof(status),"%s",lunaText("Could not open this device."));
      } else {
        BrowserEntry *entry=browserPaneEntry(pane,pane->selected);
        if(entry && entry->isDirectory) {
          if(!browserPaneEnter(pane,entry->name))snprintf(status,sizeof(status),"%s",lunaText("Could not open this folder."));
        } else if(entry && !pane->scanning) {
          if(rootAllowed)result=joinPath(path,size,pane->path,entry->name);
          else {
            int length=snprintf(path,size,"%s/%s",pane->path,entry->name);
            result=length>0 && (size_t)length<size;
          }
          if(result)break;
          snprintf(status,sizeof(status),"%s",lunaText("The current path is too long."));
        }
      }
    }
  }
  browserPaneDestroy(pane);free(pane);free(roots);return result;
}
static int appPickFile(char *path,size_t size) {
  return pickElfFile(path,size,"Choose application",NULL);
}
static int appOptionRow(int index,char *name,size_t size,char *detail,size_t detailSize,void *context) {
  (void)context;static const char *const labels[]={"Rename","Move up","Move down","Locate ELF","Remove from Apps"};
  snprintf(name,size,"%s",lunaText(labels[index]));if(detailSize)detail[0]=0;return 1;
}
static void appOptions(AppsMenu *menu) {
  int selected=0,action=-1;
  for(;;) {
    drawFileManagerList(lunaText("App options"),menu->apps->items[menu->selected].name,"",5,selected,
        appOptionRow,NULL,PROMPT_TWO(ICON_CROSS,lunaText("Select"),ICON_CIRCLE,lunaText("Back")));
    int input=waitForInput(-1);
    if(input&(PAD_CIRCLE|PAD_TRIANGLE))return;
    if(input&PAD_UP)selected=(selected+4)%5;
    else if(input&PAD_DOWN)selected=(selected+1)%5;
    else if(input&PAD_CROSS){action=selected;break;}
  }
  LunaApps *backup=malloc(sizeof(*backup));if(!backup)return;
  memcpy(backup,menu->apps,sizeof(*backup));
  int index=menu->selected,changed=0;
  LunaApp *app=&menu->apps->items[index];char path[PATH_MAX+1];
  if(action==0)changed=editFileName(lunaText("App name"),app->name,sizeof(app->name),LUNA_APP_NAME);
  else if(action==1 || action==2) {
    int other=index+(action==1?-1:1);
    if(other>=0 && other<menu->apps->count) {
      menu->apps->items[index]=backup->items[other];menu->apps->items[other]=backup->items[index];
      menu->selected=other;changed=1;
    }
  } else if(action==3) {
    if(appPickFile(path,sizeof(path))) {
        char stored[PATH_MAX+1];
        if(lunaAppsStorePath(path,stored,sizeof(stored))){free(backup);return;}
        int duplicate=0;for(int i=0;i<menu->apps->count;i++)if(i!=index && !strcmp(menu->apps->items[i].elf,stored))duplicate=1;
        if(!duplicate){strcpy(app->elf,stored);changed=1;}
        else snprintf(menu->status,sizeof(menu->status),"%s",lunaText("This app is already in Apps."));
    }
  } else if(action==4) {
    drawFileManagerList(lunaText("Remove from Apps"),app->name,lunaText("Remove this shortcut? The ELF will be kept."),
        0,0,NULL,NULL,PROMPT_TWO(ICON_CROSS,lunaText("Remove"),ICON_CIRCLE,lunaText("Cancel")));
    if(waitForInput(PAD_CROSS|PAD_CIRCLE)&PAD_CROSS) {
      memmove(app,app+1,(menu->apps->count-index-1)*sizeof(*app));menu->apps->count--;changed=1;
      if(menu->selected>=menu->apps->count)menu->selected=menu->apps->count?menu->apps->count-1:0;
    }
  }
  if(changed) {
    int error=lunaAppsSave(menu->apps);
    if(error){memcpy(menu->apps,backup,sizeof(*backup));menu->selected=index;}
    snprintf(menu->status,sizeof(menu->status),"%s",lunaText(error?appSaveError(error):"Saved"));
  } else if(action==0)memcpy(menu->apps,backup,sizeof(*backup));
  free(backup);
}

enum { APP_ACTION_LAUNCH,APP_ACTION_ADD,APP_ACTION_OPTIONS };
static const char *appHubDescription(void *context,int row) {
  AppsMenu *menu=context;
  if(menu->status[0])return menu->status;
  if(!menu->apps->count)return "No apps yet. Press Square to add an ELF.";
  if(menu->checked!=row) {
    char path[PATH_MAX+1];
    menu->available=!lunaAppsResolve(menu->apps->items[row].elf,path,sizeof(path)) && !access(path,F_OK);
    menu->checked=row;
  }
  return menu->available?menu->apps->items[row].elf:"App unavailable. Use Locate ELF in Options.";
}
static void appHubFooter(void *context,float opacity) {
  AppsMenu *menu=context;
  const ButtonPrompt empty[]={{ICON_CROSS,lunaText("Add app")},{ICON_CIRCLE,lunaText("Back")}};
  const ButtonPrompt buttons[]={{ICON_CROSS,lunaText("Launch")},{ICON_SQUARE,lunaText("Add app")},
      {ICON_TRIANGLE,lunaText("Options")},{ICON_CIRCLE,lunaText("Back")}};
  uiCubeSubmenuFooterDraw(&(PromptBar){NULL,menu->apps->count?buttons:empty,menu->apps->count?4:2},opacity);
}
static int appHubInput(void *context,int input) {
  AppsMenu *menu=context;
  if(!menu->loadError && (input&PAD_SQUARE)) {
    menu->action=APP_ACTION_ADD;return LUNA_CUBE_SUBMENU_DONE;
  }
  if(!menu->loadError && menu->apps->count && (input&PAD_TRIANGLE)) {
    menu->action=APP_ACTION_OPTIONS;return LUNA_CUBE_SUBMENU_DONE;
  }
  if(!menu->loadError && (input&(PAD_UP|PAD_DOWN|PAD_LEFT|PAD_RIGHT|PAD_L1|PAD_R1|PAD_L2|PAD_R2)))
    menu->status[0]=0;
  return LUNA_CUBE_SUBMENU_STAY;
}
static void uiAppsLoop(void) {
  AppsMenu *menu=calloc(1,sizeof(*menu));if(!menu)return;
  menu->apps=calloc(1,sizeof(*menu->apps));if(!menu->apps){free(menu);return;}
  menu->checked=-1;menu->loadError=loadAppShortcuts(menu->apps);
  if(menu->loadError)snprintf(menu->status,sizeof(menu->status),"%s",lunaText("Could not read Apps. Check the launcher drive."));
  for(;;) {
    const char *labels[LUNA_APP_LIMIT];
    for(int i=0;i<menu->apps->count;i++)labels[i]=menu->apps->items[i].name;
    if(!menu->apps->count)labels[0]="Add app";
    LunaCubeHub hub={.title="Apps",.labels=labels,.count=menu->apps->count?menu->apps->count:1,
      .confirmRow=-1,.context=menu,.description=appHubDescription,.input=appHubInput,
      .footer=appHubFooter,.scrollable=1,.literalLabels=menu->apps->count!=0};
    menu->action=APP_ACTION_LAUNCH;
    int row=uiCubeHubLoop(&menu->selected,NULL,&hub);
    if(row<0)break;
    if(menu->action==APP_ACTION_ADD || !menu->apps->count) {
      char path[PATH_MAX+1];
      if(!menu->loadError && appPickFile(path,sizeof(path))) {
        int priorCount=menu->apps->count;
        addAppShortcut(path,menu->status,sizeof(menu->status));
        menu->loadError=loadAppShortcuts(menu->apps);
        if(menu->apps->count>priorCount)menu->selected=menu->apps->count-1;
      }
    } else if(menu->action==APP_ACTION_OPTIONS)appOptions(menu);
    else {
      char path[PATH_MAX+1];
      if(!lunaAppsResolve(menu->apps->items[row].elf,path,sizeof(path))) {
        drawFileManagerList(lunaText("Launch ELF"),menu->apps->items[row].name,
            lunaText("Launch this application and leave LUNA?"),0,0,NULL,NULL,
            PROMPT_TWO(ICON_CROSS,lunaText("Launch"),ICON_CIRCLE,lunaText("Cancel")));
        if(waitForInput(PAD_CROSS|PAD_CIRCLE)&PAD_CROSS) {
          FileOpenProgress progress={.path=path,.application=1};
          const char *error=lunaHomebrewPrepareWithArgument(path,menu->apps->items[row].argument,drawFileOpenProgress,&progress);
          if(error)snprintf(menu->status,sizeof(menu->status),"%s",lunaText(error));
          else {uiCubePanelGlassClose();free(menu->apps);free(menu);lunaHomebrewExecute();}
        }
      }
    }
    menu->checked=-1;
  }
  uiCubePanelGlassClose();free(menu->apps);free(menu);
}
