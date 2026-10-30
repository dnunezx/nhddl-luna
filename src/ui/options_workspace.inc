/* Game categories use the same crystal hub and pages as Settings. */
static int owCategoryCount(const OptionsMenuState *s) {
  return s->target->platform == TARGET_PS1 ? 4 : GAME_SECTION_COUNT;
}
static GameSection owCategorySection(const OptionsMenuState *s, int row) {
  static const GameSection ps1[] = {GAME_MEMORY_CARDS,GAME_COMPATIBILITY,GAME_VIDEO,GAME_DISCS};
  return s->target->platform == TARGET_PS1 ? ps1[row] : (GameSection)row;
}
static const char *owSectionLabel(GameSection section) {
  if (section == GAME_MEMORY_CARDS) return lunaText("Memory Cards");
  if (section == GAME_VIDEO_OUT) return lunaText("Video Output");
  if (section == GAME_DISCS) return lunaText("Discs");
  return lunaText(gameSectionLabels[section]);
}
static int owRowCount(const OptionsMenuState *s, GameSection section) {
  if (s->target->platform == TARGET_PS1)
    return section == GAME_MEMORY_CARDS ? 4 : section == GAME_DISCS ? 2 : 1;
  if (section == GAME_CHEATS) return 3 + s->cheatFile.entryCount;
  if (section == GAME_MEMORY_CARDS) return gameVMCEnabled(s->titleArguments) ? 4 : 3;
  return gameSectionRowCount(s,section);
}
static void owDescribeRow(OptionsMenuState *s, GameSection section, int row,
                          OptionsWorkspaceRow *out) {
  memset(out,0,sizeof(*out));
  const char *label="", *value="", *help="";
  if (s->target->platform == TARGET_PS1) {
    if (section == GAME_MEMORY_CARDS) {
      static const char *const labels[]={"Card assignment","Slot 1","Slot 2","Manage cards"};
      label=labels[row];
#ifdef LUNA_ENABLE_PSXCORE
      value=!s->ps1CardsReady ? s->vmcStatus[0] ? "Unavailable" : "Open" : row==0 ? s->ps1SetName :
            row<3 ? s->ps1SlotStatus[row-1] : ">";
#endif
      help="Games using the same cards share their saved data. Missing default cards are created at launch.";
    } else if (section == GAME_DISCS) {
      unsigned count=s->target->discs ? s->target->discs->count : 1;
      int disc=s->gameOptions.ps1StartDisc;
      label=row ? "Manage discs" : "Start disc";
      if (row) value=">";
      else if (disc<0 || (unsigned)disc>=count) value="Unavailable";
      else snprintf(out->value,sizeof(out->value),"%s %d / %u",lunaText("Disc"),disc+1,count);
      help=s->target->discs && s->target->discs->error[0] ? s->target->discs->error :
        "All discs use the same memory cards. Swapping controls are shown before launch.";
    } else {
      LunaGameRow option=section==GAME_VIDEO ? LUNA_GAME_PS1_HDTV_FIX : LUNA_GAME_PS1_CODE_VALIDATION;
      label=gameRowLabels[option]; value=lunaGameOptionsValue(&s->gameOptions,option);
      help=gameRowDescriptions[option];
    }
  } else if (section == GAME_MEMORY_CARDS) {
    static const char *const labels[]={"VMC slot 1","VMC slot 2","Card assignment","Disable virtual cards"};
    label=labels[row]; value=row<2 ? s->gameOptions.vmcSlotLabel[row] : ">";
    help=row==2 ? "Create cards for this game, choose existing cards or use shared cards." :
         row==3 ? "Use physical cards in both slots for this game." : gameRowDescriptions[LUNA_GAME_VMC_SLOT1+row];
  } else if (section == GAME_CHEATS) {
    if (row==0) { label="Enable cheats"; value=s->cheats.enabled ? "On" : "Off"; }
    else if (row==1) { label="Clear selections"; value=">"; }
    else if (row==2) { label="Reload file"; value=">"; }
    else {
      const LunaCheatEntry *entry=&s->cheatFile.entries[row-3];
      label=entry->name;
      value=entry->required ? "Required" : lunaCheatSelected(&s->cheats,entry->id) ? "On" : "Off";
      snprintf(out->description,sizeof(out->description),"%s: %d %s. %s",entry->name,entry->pairCount,
          lunaText("lines"),lunaText(entry->required ? "Included with selected cheats." : "Toggle this whole code block."));
    }
    if (s->cheatLoadResult || row==2) help=s->cheatStatus;
    else if (row<3) snprintf(out->description,sizeof(out->description),"%d %s. %s",s->cheats.count,
        lunaText("selected"),lunaText(s->cheats.enabled ? "Changes save automatically. Square to test." :
                                                      "Cheats are off; selections are retained."));
  } else if (section == GAME_VIDEO_OUT) {
    label=lunaGameVideoModeLabel(gameUsesOpl(s),row);
    value=(gameUsesOpl(s) ? s->gameOptions.oplVideoMode : s->gameOptions.videoMode)==row ? "On" : "Off";
    help=row==0 ? "Keep the game's original output." :
                  "Select an output for the selected game core.";
  } else {
    LunaGameRow option=gameSectionRow(s,section,row);
    label=gameRowLabels[option]; help=gameRowDescriptions[option];
    value=option==LUNA_GAME_PS2_LOGO ? gamePS2LogoValue(s) : lunaGameOptionsValue(&s->gameOptions,option);
  }
  snprintf(out->label,sizeof(out->label),"%s",section==GAME_CHEATS && row>=3 ? label : lunaText(label));
  if (!out->value[0]) snprintf(out->value,sizeof(out->value),"%s",lunaText(value));
  if (help[0]) snprintf(out->description,sizeof(out->description),"%s",lunaText(help));
}
static void owSummary(OptionsMenuState *s, GameSection section, char *out, size_t size) {
  if (section==GAME_MEMORY_CARDS) {
    snprintf(out,size,"%s",lunaText(s->target->platform==TARGET_PS1 ?
#ifdef LUNA_ENABLE_PSXCORE
        s->ps1CardsReady ? s->ps1SetName : s->vmcStatus[0] ? "Unavailable" : "Open to load cards"
#else
        "Shared cards"
#endif
        : gameVMCEnabled(s->titleArguments) ? "Enabled" : "Physical cards"));
  } else if (section==GAME_COMPATIBILITY) {
    int enabled=0;
    if (s->target->platform==TARGET_PS1) enabled=s->gameOptions.ps1CodeValidation!=0;
    else {
      unsigned mask=gameUsesOpl(s) ? s->gameOptions.oplCompat : s->gameOptions.compat;
      for (;mask;mask>>=1) enabled+=mask&1;
      if (!gameUsesOpl(s)) enabled+=s->gameOptions.neutrinoIgrDisabled!=0;
    }
    if (enabled) snprintf(out,size,lunaText("%d enabled"),enabled);
    else snprintf(out,size,"%s",lunaText("Default"));
  } else if (section==GAME_CHEATS) {
    snprintf(out,size,"%s / %d %s",lunaText(s->cheats.enabled ? "On" : "Off"),s->cheats.count,lunaText("selected"));
  } else if (section==GAME_LAUNCH) {
    snprintf(out,size,"%d %s",gameLaunchEnabled(s),lunaText("enabled"));
  } else if (section==GAME_DISCS) {
    snprintf(out,size,"%u %s",s->target->discs ? s->target->discs->count : 1,lunaText("discs"));
  } else if (section==GAME_VIDEO_OUT) {
    snprintf(out,size,"%s",gameUsesOpl(s) ? "OPL" : "Neutrino");
  } else {
    LunaGameRow option=section==GAME_CORE ? LUNA_GAME_CORE : s->target->platform==TARGET_PS1 ?
        LUNA_GAME_PS1_HDTV_FIX : gameUsesOpl(s) ? LUNA_GAME_OPL_VIDEO_MODE : LUNA_GAME_VIDEO_MODE;
    snprintf(out,size,"%s",lunaGameOptionsValue(&s->gameOptions,option));
  }
}

static int optionsCubeBackground(void *context,int closing) {
  OptionsMenuState *s=context;
  if(closing && s->transition.opening)
    lunaOptionsRevealStart(&s->transition,0,uiNowMs());
  int done=lunaOptionsRevealTick(&s->transition,uiNowMs());
  if(s->scene && s->scene->drawLibrary)
    s->scene->drawLibrary(s->scene->context,s->transition.position);
  else drawSharedLibraryBackground(uiNowMs());
  gsKit_set_test(gsGlobal,GS_ZTEST_OFF);
  gsGlobal->PrimAlphaEnable=GS_SETTING_ON;
  gsKit_set_primalpha(gsGlobal,GS_SETREG_ALPHA(0,1,0,1,0),0);
  gsKit_set_test(gsGlobal,GS_ATEST_OFF);
  gsKit_prim_sprite(gsGlobal,0,0,gsGlobal->Width,gsGlobal->Height,1,
      GS_SETREG_RGBA(0,0,0,(int)(128*s->transition.shade)));
  return closing && done;
}

static void optionsWorkspaceBackdrop(void *context) {
  (void)context;
  uiCubeHubDrawBackdrop();
  gsKit_set_test(gsGlobal,GS_ZTEST_OFF);
}

static const char *optionsCubeDescription(void *context,int row) {
  OptionsMenuState *s=context;
  if(s->saveError)return s->saveErrorLabel?s->saveErrorLabel:"Could not save game settings";
  owSummary(s,owCategorySection(s,row),s->workspaceHelp,sizeof(s->workspaceHelp));
  return s->workspaceHelp;
}

static const char *optionsCubeLabel(void *context,int row) {
  OptionsMenuState *s=context;
  owDescribeRow(s,s->gameSection,row,&s->workspaceRow);
  return s->workspaceRow.label;
}

static const char *optionsCubeValue(void *context,int row) {
  OptionsMenuState *s=context;
  owDescribeRow(s,s->gameSection,row,&s->workspaceRow);
  return s->workspaceRow.value;
}

static const char *optionsCubeRowDescription(void *context,int row) {
  OptionsMenuState *s=context;
  if(s->saveError)return s->saveErrorLabel?s->saveErrorLabel:"Could not save game settings";
  if(s->vmcStatus[0])return s->vmcStatus;
  owDescribeRow(s,s->gameSection,row,&s->workspaceRow);
  return s->workspaceRow.description;
}

static int optionsCubeAction(OptionsMenuState *s,int input) {
  int result=handleGameInput(s,input);
  if(result) {
    s->result=result==STORAGE_UI_REFRESH?result:result<0?-1:0;
    return LUNA_CUBE_SUBMENU_DONE;
  }
  return LUNA_CUBE_SUBMENU_STAY;
}

static void optionsCubeAdjust(void *context,int row,int direction) {
  OptionsMenuState *s=context;
  s->selectedGameRow=row;
  optionsCubeAction(s,direction<0?PAD_LEFT:PAD_RIGHT);
}

typedef struct { OptionsMenuState *state; int row; } OptionsCubeCategory;

static void optionsCubeBegin(void *context) {
  OptionsCubeCategory *category=context;
  OptionsMenuState *s=category->state;
  s->selectedGameHubRow=category->row;
  s->gameSection=owCategorySection(s,category->row);
  s->selectedGameRow=s->workspaceRows[s->gameSection];
  s->vmcStatus[0]=0;
  if(s->gameSection==GAME_CHEATS)reloadGameCheats(s,0);
#ifdef LUNA_ENABLE_PSXCORE
  if(s->target->platform==TARGET_PS1 && s->gameSection==GAME_MEMORY_CARDS && !s->ps1CardsReady)
    loadPs1Cards(s);
#endif
  s->pageFocus=(LunaCubePageFocus){.selected=-1,.glideMs=110};
}

static void optionsCubeDraw(void *context,float opacity) {
  OptionsMenuState *s=((OptionsCubeCategory *)context)->state;
  int count=owRowCount(s,s->gameSection);
  if(s->selectedGameRow<0 || s->selectedGameRow>=count)s->selectedGameRow=0;
  static const char *const coreTabs[]={"Neutrino","OPL"};
  int showCoreTabs=s->target->platform!=TARGET_PS1 &&
      (s->gameSection==GAME_COMPATIBILITY || s->gameSection==GAME_VIDEO ||
       s->gameSection==GAME_VIDEO_OUT);
  LunaCubePage page={.title=owSectionLabel(s->gameSection),.count=count,.label=optionsCubeLabel,
    .value=optionsCubeValue,.description=optionsCubeRowDescription,
    .adjust=optionsCubeAdjust,.context=s,.sectionTitle="Game Options",
    .subtitle=s->target->name,.libraryFooter=1,
    .stackedValues=s->gameSection==GAME_MEMORY_CARDS || s->gameSection==GAME_CORE,
    .tabs=showCoreTabs?coreTabs:NULL,.tabCount=showCoreTabs?2:0,
    .selectedTab=gameUsesOpl(s)};
  uiCubeSubmenuPageDraw(&page,s->selectedGameRow,&s->pageFocus,opacity);
  if(!s->saveError && s->savedAt && uiNowMs()-s->savedAt<1800U)
    drawTextWindow(gsGlobal->Width*525/640,gsGlobal->Height*22/480,
        gsGlobal->Width*608/640,gsGlobal->Height*50/480,7,
        GS_SETREG_RGBA(235,247,255,(int)(128*opacity)),ALIGN_RIGHT,lunaText("Saved"));
}

static int optionsCubeInput(void *context,int input) {
  OptionsMenuState *s=((OptionsCubeCategory *)context)->state;
  if(input&(PAD_TRIANGLE|PAD_CIRCLE|PAD_START)) {
    if(s->gameSection!=GAME_VIDEO_OUT)return LUNA_CUBE_SUBMENU_BACK;
    s->workspaceRows[GAME_VIDEO_OUT]=s->selectedGameRow;
    s->gameSection=GAME_VIDEO;
    s->selectedGameRow=s->workspaceRows[GAME_VIDEO];
    s->pageFocus.selected=-1;
    return LUNA_CUBE_SUBMENU_STAY;
  }
  if(input&PAD_L1)input=(input&~PAD_L1)|PAD_UP;
  if(input&PAD_R1)input=(input&~PAD_R1)|PAD_DOWN;
  GameSection before=s->gameSection;
  s->workspaceRows[before]=s->selectedGameRow;
  int result=optionsCubeAction(s,input);
  if(before!=s->gameSection)s->pageFocus.selected=-1;
  return result;
}

static void optionsCubeEnd(void *context) {
  OptionsMenuState *s=((OptionsCubeCategory *)context)->state;
  s->workspaceRows[s->gameSection]=s->selectedGameRow;
  s->gameSection=GAME_HUB;
}

static int optionsCubeHubInput(void *context,int input) {
  return (input&PAD_SQUARE)?optionsCubeAction(context,PAD_SQUARE):LUNA_CUBE_SUBMENU_STAY;
}

static void optionsCubeFooter(void *context,float pageProgress) {
  OptionsMenuState *s=context;
  uiCubeOptionsFooterTransitionDraw(s->transition.position,pageProgress);
}

static void optionsCubeLoop(OptionsMenuState *s) {
  OptionsCubeCategory contexts[GAME_SECTION_COUNT];
  LunaCubeSubmenu submenus[GAME_SECTION_COUNT];
  const char *labels[GAME_SECTION_COUNT];
  int count=owCategoryCount(s);
  for(int i=0;i<count;i++) {
    contexts[i]=(OptionsCubeCategory){s,i};
    labels[i]=owSectionLabel(owCategorySection(s,i));
    submenus[i]=(LunaCubeSubmenu){.context=&contexts[i],.begin=optionsCubeBegin,
      .draw=optionsCubeDraw,.input=optionsCubeInput,.end=optionsCubeEnd};
  }
  LunaCubeHub hub={.title="Game Options",.labels=labels,.count=count,
    .submenus=submenus,.submenuCount=count,.confirmRow=-1,.context=s,
    .description=optionsCubeDescription,.subtitle=s->target->name,
    .libraryFooter=1,.gameOptionsLogo=1,
    .background=optionsCubeBackground,.input=optionsCubeHubInput,.footer=optionsCubeFooter};
  uiCubeHubLoop(&s->selectedGameHubRow,NULL,&hub);
}
