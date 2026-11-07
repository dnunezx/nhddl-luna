/* Appearance categories share the Settings crystals and reversible portals. */
static const int appearanceCubeCategories[]={AP_THEMES,AP_BACKGROUND,AP_VIEWS,AP_COLORS,AP_TEXT,AP_SOUND};
#define AP_CUBE_CATEGORIES (sizeof(appearanceCubeCategories)/sizeof(*appearanceCubeCategories))
typedef struct { AppearanceEditor *editor; int category; } AppearanceCubeCategory;
static int appearanceWorkspaceEffects(const AppearanceEditor *s) {
  return s->pending.background==LIBRARY_BACKGROUND_ORBS?4+ORB_SHAPE_PLAYTIME:
    s->pending.background==LIBRARY_BACKGROUND_SPACE_FLIGHT?2:
    s->pending.background==LIBRARY_BACKGROUND_NEO?3:0;
}
static int appearanceWorkspaceRows(AppearanceEditor *s) {
  return s->category==AP_BACKGROUND?1+appearanceWorkspaceEffects(s):appearanceRowCount(s);
}
static const char *appearanceWorkspaceRowText(AppearanceEditor *s,int row,int value) {
  int category=s->category;
  if(category==AP_BACKGROUND&&row>0) {s->category=AP_EFFECTS;row--;}
  const char *text=value?appearanceRowValue(s,row):appearanceRowLabel(s,row);
  s->category=category;return text;
}
static const char *appearanceWorkspaceDescription(void *context,int row) {
  AppearanceEditor *s=context;
  if(s->message)return s->message;
  if(s->category==AP_BACKGROUND&&row==0)return appearanceWorkspaceEffects(s)?
    "Left / Right chooses a background. Down opens its effects.":
    "Left / Right chooses a background.";
  int category=s->category;
  if(category==AP_BACKGROUND&&row>0) {s->category=AP_EFFECTS;row--;}
  const char *text=appearanceRowDescription(s,row);s->category=category;return text;
}
static void appearanceWorkspaceAdjust(void *context,int row,int direction) {
  AppearanceEditor *s=context;
  int category=s->category;
  if(category==AP_BACKGROUND&&row>0) {s->category=AP_EFFECTS;row--;}
  appearanceAdjust(s,row,direction);s->category=category;
  if(s->rows[category]>=appearanceWorkspaceRows(s))s->rows[category]=0;
}
static const char *appearanceCubeDescription(void *context,int row) {
  return appearanceHubDescription(context,appearanceCubeCategories[row]);
}
static const char *appearanceCubeLabel(void *context,int row) {
  return appearanceWorkspaceRowText(context,row,0);
}
static const char *appearanceCubeValue(void *context,int row) {
  return appearanceWorkspaceRowText(context,row,1);
}
static void appearanceCubeBegin(void *context) {
  AppearanceCubeCategory *category=context;
  AppearanceEditor *s=category->editor;
  s->category=category->category;
  if(s->rows[s->category]>=appearanceWorkspaceRows(s))s->rows[s->category]=0;
  s->pageFocus=(LunaCubePageFocus){.selected=-1,.glideMs=110};
}
static void appearanceCubeDraw(void *context,float opacity) {
  AppearanceEditor *s=((AppearanceCubeCategory *)context)->editor;
  LunaCubePage page={.title=categories[s->category],.count=appearanceWorkspaceRows(s),
    .label=appearanceCubeLabel,.value=appearanceCubeValue,
    .description=appearanceWorkspaceDescription,.adjust=appearanceWorkspaceAdjust,
    .context=s,.sectionTitle="Appearance"};
  uiCubeSubmenuPageDraw(&page,s->rows[s->category],&s->pageFocus,opacity);
  uint64_t ink=GS_SETREG_RGBA(235,247,255,(int)(128*opacity));
  if(s->savedAt&&uiNowMs()-s->savedAt<1800U)
    drawTextWindow(gsGlobal->Width*525/640,gsGlobal->Height*24/480,
        gsGlobal->Width*608/640,gsGlobal->Height*50/480,7,ink,ALIGN_RIGHT,lunaText("Saved"));
  if(s->category==AP_TEXT) {
    drawTextWindow(gsGlobal->Width*48/640,gsGlobal->Height*218/480,
        gsGlobal->Width*490/640,gsGlobal->Height*250/480,7,ink,ALIGN_LEFT,"Aa Bb Cc 0123456789");
    drawTextWindow(gsGlobal->Width*48/640,gsGlobal->Height*260/480,
        gsGlobal->Width*490/640,gsGlobal->Height*292/480,7,ink,ALIGN_LEFT,lunaText("Game Library / Settings"));
  }
}
static int appearanceCubeInput(void *context,int input) {
  AppearanceEditor *s=((AppearanceCubeCategory *)context)->editor;
  if(input&(PAD_CIRCLE|PAD_TRIANGLE|PAD_START))return LUNA_CUBE_SUBMENU_BACK;
  int count=appearanceWorkspaceRows(s),*row=&s->rows[s->category];
  if(*row>=count)*row=0;
  if(input&(PAD_UP|PAD_L1))*row=(*row+count-1)%count;
  else if(input&(PAD_DOWN|PAD_R1))*row=(*row+1)%count;
  else if(s->category==AP_THEMES&&*row==1) {
    if(input&PAD_CROSS)appearanceWorkspaceAdjust(s,*row,0);
  } else if(input&(PAD_LEFT|PAD_RIGHT|PAD_CROSS))
    appearanceWorkspaceAdjust(s,*row,input&PAD_LEFT?-1:1);
  return LUNA_CUBE_SUBMENU_STAY;
}
static void appearanceCubeLoop(AppearanceEditor *s) {
  AppearanceCubeCategory categoriesContext[AP_CUBE_CATEGORIES];
  LunaCubeSubmenu submenus[AP_CUBE_CATEGORIES];
  const char *labels[AP_CUBE_CATEGORIES];
  for(int i=0;i<AP_CUBE_CATEGORIES;i++) {
    categoriesContext[i]=(AppearanceCubeCategory){s,appearanceCubeCategories[i]};
    labels[i]=categories[appearanceCubeCategories[i]];
    submenus[i]=(LunaCubeSubmenu){.context=&categoriesContext[i],.begin=appearanceCubeBegin,
      .draw=appearanceCubeDraw,.input=appearanceCubeInput};
  }
  LunaCubeHub hub={.title="Appearance",.labels=labels,.count=AP_CUBE_CATEGORIES,
    .submenus=submenus,.submenuCount=AP_CUBE_CATEGORIES,.confirmRow=-1,
    .context=s,.description=appearanceCubeDescription};
  int selected=0;
  uiCubeHubLoop(&selected,NULL,&hub);
}
