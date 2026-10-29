/* Standalone launcher appearance editor; game-specific options live elsewhere. */
#include "ui/appearance_menu.h"
#include "ui/appearance.h"
#include "ui/ring_menu.h"
#include "ui/cube_menu.h"
#include "ui/view_internal.h"
#include "ui/view_state.h"
#include "ui/language.h"
#include "ui/pad.h"
#include "ui/navigation.h"
#include "ui/ambient.h"
#include "ui/space_flight.h"
#include "options.h"
#include <libpad.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum { AP_BACKGROUND,AP_VIEWS,AP_COLORS,AP_EFFECTS,AP_TEXT,AP_SOUND };
static const char *const categories[]={"Backgrounds","Library Views","Interface Colors",
  "Background Effects","Text","Ambient Sound"};
static const char *const backgrounds[]={"Stars & Cubes","Ambient Orbs","Red Clouds","Midnight Cubes",
  "System Configuration","Neo","Aurora","Ronin","Torch","Tempest","Space Flight"};
static const char *const colors[]={"Original","Cyan","Violet","Rose","Green","Gold","White"};
static const char *const shapes[]={"Diamond","Cube","Octahedron","Sphere","LUNA","Skull","Atom","Iron Cross"};
typedef struct {
  Target *target;
  LunaAppearance saved,pending,runtime;
  int category,loadError;
  uint32_t savedAt;
  const char *message;
  char value[96];
  int rows[6];
  LunaCubePageFocus pageFocus;
} AppearanceEditor;

static void appearanceReleaseButtons(void) {
  while(pollInput()&(PAD_CROSS|PAD_CIRCLE|PAD_TRIANGLE|PAD_START))usleep(1000);
}
static int appearancePreview(AppearanceEditor *s,const LunaAppearance *a) {
  const LunaAppearance *old=&s->runtime;
  if(a->background!=old->background&&setLibraryBackground((LibraryBackground)a->background)) {
    setLibraryBackground((LibraryBackground)old->background);return -1;
  }
  if(a->font!=old->font&&setUIFont((UIFont)a->font)) {
    setLibraryBackground((LibraryBackground)old->background);return -1;
  }
  if(a->orbsAppearance!=old->orbsAppearance&&setAmbientOrbsAppearance((AmbientOrbsAppearance)a->orbsAppearance)) {
    setLibraryBackground((LibraryBackground)old->background);setUIFont((UIFont)old->font);return -1;
  }
  setGlassColorPreset((GlassColorPreset)a->glassColor);
  uiRingSetColor((LunaRingColor)a->ringColor);
  uiRingSetEffectsColor((LunaRingColor)a->ringEffectsColor);
  setClassicArtOverlap(a->classicOverlap);setPs1CaseStyle((Ps1CaseStyle)a->ps1CaseStyle);
  setCollectionGameList(a->collectionGameList);ambientSetEnabled(a->ambientSound);
  if(a->orbsTheme!=old->orbsTheme)setAmbientOrbsTheme((AmbientOrbsTheme)a->orbsTheme,uiNowMs());
  if(a->orbShapes!=old->orbShapes)setAmbientOrbsShapes(a->orbShapes,uiNowMs());
  setAmbientOrbsColor(ORBS_COLOR_PART_ORBS,(AmbientOrbsColor)a->orbsColor);
  setAmbientOrbsColor(ORBS_COLOR_PART_TAILS,(AmbientOrbsColor)a->tailsColor);
  lunaSpaceSetSpeed(a->spaceSpeed);lunaSpaceSetDepth(a->spaceDepth);
  s->runtime=*a;return 0;
}
static const char *appearanceHubDescription(void *context,int row) {
  AppearanceEditor *s=context;
  if(s->message)return s->message;
  static const char *const descriptions[]={
    "Preview the background used throughout LUNA.","Choose library views and cover layouts.",
    "Choose the glass tint and Start-menu ring color.","Tune Ambient Orbs or Space Flight.",
    "Choose the interface font. Wide menu headings stay unchanged.","Play ambient music while browsing."};
  return descriptions[row];
}
static int appearanceRowCount(AppearanceEditor *s) {
  if(s->category==AP_VIEWS)return UI_VIEW_COUNT+3;
  if(s->category==AP_COLORS)return 3;
  if(s->category==AP_EFFECTS)return s->pending.background==LIBRARY_BACKGROUND_SPACE_FLIGHT?2:4+ORB_SHAPE_PLAYTIME;
  return 1;
}
static const char *appearanceRowLabel(void *context,int row) {
  AppearanceEditor *s=context;
  switch(s->category) {
    case AP_BACKGROUND:return "Background";
    case AP_COLORS:return (const char *const[]){"Glass color","Ring color","Menu effects color"}[row];
    case AP_TEXT:return "Font";
    case AP_SOUND:return "Ambient sound";
    case AP_VIEWS:
      if(row<UI_VIEW_COUNT)return lunaNavViewLabel(lunaViewCycleOrder[row]);
      return (const char *const[]){"Cover and disc layout","PS1 case style","Collections game list"}[row-UI_VIEW_COUNT];
    default:
      if(s->pending.background==LIBRARY_BACKGROUND_SPACE_FLIGHT)return row?"Depth":"Speed";
      if(row<4)return (const char *const[]){"Behavior","Appearance","Orb color","Tail color"}[row];
      return shapes[row-4];
  }
}
static const char *appearanceRowValue(void *context,int row) {
  AppearanceEditor *s=context;LunaAppearance *a=&s->pending;int value=0;
  switch(s->category) {
    case AP_BACKGROUND:return backgrounds[a->background];
    case AP_COLORS:return row?
      (const char *const[]){"Original","Cyan","Blue","Violet","Rose","Green","White"}[row==1?a->ringColor:a->ringEffectsColor]:
      (const char *const[]){"Original","Luminous","Cosmic","Cyber"}[a->glassColor];
    case AP_TEXT:return a->font==UI_FONT_PSBBN?"PSBBN":"DejaVu";
    case AP_SOUND:value=a->ambientSound;break;
    case AP_VIEWS:
      if(row<UI_VIEW_COUNT)value=(a->enabledViews>>lunaViewCycleOrder[row])&1;
      else if(row==UI_VIEW_COUNT)return a->classicOverlap?"Overlap":"Separate";
      else if(row==UI_VIEW_COUNT+1)return a->ps1CaseStyle==PS1_CASE_SQUARE?"Square":"Tall";
      else value=a->collectionGameList;
      break;
    default:
      if(a->background==LIBRARY_BACKGROUND_SPACE_FLIGHT) {
        snprintf(s->value,sizeof(s->value),"%u",(unsigned)(row?a->spaceDepth:a->spaceSpeed));return s->value;
      }
      if(a->background!=LIBRARY_BACKGROUND_ORBS)return "Ambient Orbs required";
      if(row==0)return a->orbsTheme==ORBS_THEME_LUNA?"LUNA":"PS2 Original";
      if(row==1)return a->orbsAppearance==ORBS_APPEARANCE_LUNA?"LUNA":"PS2 Original";
      if(row<4)return a->orbsAppearance==ORBS_APPEARANCE_PS2_ORIGINAL?"Original colors":colors[row==2?a->orbsColor:a->tailsColor];
      if(a->orbsTheme!=ORBS_THEME_LUNA)return "LUNA behavior required";
      value=(a->orbShapes>>(row-4))&1;
  }
  return value?"On":"Off";
}
static const char *appearanceRowDescription(void *context,int row) {
  AppearanceEditor *s=context;
  if(s->message)return s->message;
  if(s->category==AP_COLORS&&row==1)return "Changes the Start-menu ring.";
  if(s->category==AP_COLORS&&row==2)return "Changes the Start-menu orbs, trails and bursts.";
  if(s->category==AP_EFFECTS) {
    if(s->pending.background==LIBRARY_BACKGROUND_SPACE_FLIGHT)return "Changes save automatically.";
    if(s->pending.background!=LIBRARY_BACKGROUND_ORBS)return "Choose Ambient Orbs or Space Flight under Backgrounds.";
    if(row>=4)return "Choose LUNA formations. All off keeps playtime.";
    if(row>=2&&s->pending.orbsAppearance==ORBS_APPEARANCE_PS2_ORIGINAL)return "PS2 original appearance uses its original colors.";
  }
  return "Changes save automatically.";
}
static void appearanceAdjust(void *context,int row,int direction) {
  AppearanceEditor *s=context;s->message=NULL;
  if(s->loadError) {
    LunaAppearance recovered;
    s->savedAt=0;
    s->loadError=loadAppearance(s->target,&recovered);
    if(s->loadError) {s->message="Could not read appearance settings";return;}
    s->saved=s->pending=recovered;
  }
  LunaAppearance candidate=s->pending;
  switch(s->category) {
    case AP_BACKGROUND:candidate.background=(candidate.background+LIBRARY_BACKGROUND_COUNT+direction)%LIBRARY_BACKGROUND_COUNT;break;
    case AP_COLORS:
      if(row==2)candidate.ringEffectsColor=(candidate.ringEffectsColor+LUNA_RING_COLOR_COUNT+direction)%LUNA_RING_COLOR_COUNT;
      else if(row==1)candidate.ringColor=(candidate.ringColor+LUNA_RING_COLOR_COUNT+direction)%LUNA_RING_COLOR_COUNT;
      else candidate.glassColor=(candidate.glassColor+GLASS_COLOR_COUNT+direction)%GLASS_COLOR_COUNT;
      break;
    case AP_TEXT:candidate.font=(candidate.font+UI_FONT_COUNT+direction)%UI_FONT_COUNT;break;
    case AP_SOUND:candidate.ambientSound^=1;break;
    case AP_VIEWS:
      if(row<UI_VIEW_COUNT) {
        uint32_t bit=1U<<lunaViewCycleOrder[row];
        if(candidate.enabledViews==bit) {s->message="Keep at least one library view enabled.";return;}
        candidate.enabledViews^=bit;
      } else if(row==UI_VIEW_COUNT)candidate.classicOverlap^=1;
      else if(row==UI_VIEW_COUNT+1)candidate.ps1CaseStyle^=1;
      else candidate.collectionGameList^=1;
      break;
    default:
      if(candidate.background==LIBRARY_BACKGROUND_SPACE_FLIGHT) {
        int value=(int)(row?candidate.spaceDepth:candidate.spaceSpeed)+direction;
        int maximum=row?4:8;
        if(value<0)value=0;
        if(value>maximum)value=maximum;
        if(row)candidate.spaceDepth=value;else candidate.spaceSpeed=value;
        break;
      }
      if(candidate.background!=LIBRARY_BACKGROUND_ORBS)return;
      if(row==0)candidate.orbsTheme^=1;
      else if(row==1)candidate.orbsAppearance^=1;
      else if(row<4) {
        if(candidate.orbsAppearance==ORBS_APPEARANCE_PS2_ORIGINAL)return;
        uint32_t *color=row==2?&candidate.orbsColor:&candidate.tailsColor;
        *color=(*color+ORBS_COLOR_COUNT+direction)%ORBS_COLOR_COUNT;
      } else {
        if(candidate.orbsTheme!=ORBS_THEME_LUNA)return;
        candidate.orbShapes^=1U<<(row-4);
      }
  }
  if(!memcmp(&candidate,&s->pending,sizeof(candidate)))return;
  s->savedAt=0;
  if(appearancePreview(s,&candidate)) {s->message="Could not preview appearance";return;}
  if(saveAppearance(s->target,&candidate)) {
    s->message=appearancePreview(s,&s->saved)?"Could not restore appearance":"Could not save appearance settings";
    s->pending=s->runtime;
    return;
  }
  s->pending=s->saved=candidate;
  s->savedAt=uiNowMs();
}
#include "appearance_workspace.inc"
int uiAppearanceLoop(Target *target) {
  AppearanceEditor state={.target=target};
  // The file browser or another application may have removed the settings.
  resetAppearanceCache();
  state.loadError=loadAppearance(target,&state.saved);
  state.pending=state.runtime=state.saved;
  // Empty-library entry has not run the library's appearance initialization.
  int previewError=setLibraryBackground((LibraryBackground)state.saved.background);
  if(setUIFont((UIFont)state.saved.font))previewError=1;
  if(setAmbientOrbsAppearance((AmbientOrbsAppearance)state.saved.orbsAppearance))previewError=1;
  setAmbientOrbsTheme((AmbientOrbsTheme)state.saved.orbsTheme,uiNowMs());
  setAmbientOrbsShapes(state.saved.orbShapes,uiNowMs());
  appearancePreview(&state,&state.saved);
  if(state.loadError)state.message="Could not read appearance settings";
  else if(previewError)state.message="Could not preview appearance";
  appearanceCubeLoop(&state);appearanceReleaseButtons();return 0;
}

enum { DEFAULT_LOGO, DEFAULT_CORE, DEFAULT_STARTUP_VIEW, DEFAULT_COUNT };
typedef struct {
  Target *target;int logo,core;const char *error;
  UILibraryView startupView;
  uint32_t enabledViews;
  int selected;LunaCubePageFocus focus;
} DefaultsEditor;
static const char *defaultsLabel(void *context,int row) {
  (void)context;
  return (const char *const[]){"PlayStation 2 logo","Game core","Startup view"}[row];
}
static const char *defaultsValue(void *context,int row) {
  DefaultsEditor *s=context;
  if(row==DEFAULT_STARTUP_VIEW)return lunaNavViewLabel(s->startupView);
  return row==DEFAULT_CORE?(s->core?"OPL":"Neutrino"):(s->logo?"On":"Off");
}
static const char *defaultsDescription(void *context,int row) {
  DefaultsEditor *s=context;
  if(s->error)return s->error;
  if(row==DEFAULT_STARTUP_VIEW)return "Choose the enabled library view LUNA opens at startup.";
  return row==DEFAULT_CORE?"Default game core. Each game can override it.":"Default PS2 startup logo. Each game can override it.";
}
static void defaultsAdjust(void *context,int row,int direction) {
  DefaultsEditor *s=context;
  if(row==DEFAULT_STARTUP_VIEW) {
    int index=0;
    while(index<UI_VIEW_COUNT&&lunaViewCycleOrder[index]!=s->startupView)index++;
    UILibraryView candidate=s->startupView;
    for(int step=1;step<=UI_VIEW_COUNT;step++) {
      candidate=lunaViewCycleOrder[(index+UI_VIEW_COUNT+direction*step)%UI_VIEW_COUNT];
      if(s->enabledViews&(1U<<candidate))break;
    }
    if(candidate==s->startupView)return;
    if(saveStartupLibraryView(s->target,candidate))s->error="Could not save global settings";
    else {s->startupView=candidate;s->error=NULL;}
    return;
  }
  int result=row==DEFAULT_CORE?saveGameCoreOpl(s->target,!s->core):savePS2LogoEnabled(s->target,!s->logo);
  if(result)s->error="Could not save global settings";
  else {if(row==DEFAULT_CORE)s->core^=1;else s->logo^=1;s->error=NULL;}
}
static void defaultsSubmenuBegin(void *context) {
  DefaultsEditor *state=context;
  state->logo=loadPS2LogoEnabled(state->target);state->core=loadGameCoreOpl(state->target);
  state->enabledViews=loadEnabledLibraryViews(state->target);
  state->startupView=loadStartupLibraryView(state->target);
  if(!(state->enabledViews&(1U<<state->startupView)))
    state->startupView=lunaNavNextView(state->startupView,state->enabledViews);
  state->error=NULL;state->selected=0;
  state->focus=(LunaCubePageFocus){.selected=-1,.glideMs=150};
}
static void defaultsSubmenuDraw(void *context,float opacity) {
  DefaultsEditor *state=context;
  LunaCubePage page={.title="Defaults",.count=DEFAULT_COUNT,.label=defaultsLabel,
    .value=defaultsValue,.description=defaultsDescription,.adjust=defaultsAdjust,.context=state};
  uiCubeSubmenuPageDraw(&page,state->selected,&state->focus,opacity);
}
static int defaultsSubmenuInput(void *context,int input) {
  DefaultsEditor *state=context;
  if(input&(PAD_CIRCLE|PAD_TRIANGLE|PAD_START))return LUNA_CUBE_SUBMENU_BACK;
  if(input&(PAD_UP|PAD_L1))state->selected=(state->selected+DEFAULT_COUNT-1)%DEFAULT_COUNT;
  else if(input&(PAD_DOWN|PAD_R1))state->selected=(state->selected+1)%DEFAULT_COUNT;
  else if(input&(PAD_LEFT|PAD_RIGHT|PAD_CROSS))
    defaultsAdjust(state,state->selected,input&PAD_LEFT?-1:1);
  return LUNA_CUBE_SUBMENU_STAY;
}
LunaCubeSubmenu uiDefaultsSubmenu(Target *target) {
  static DefaultsEditor state;
  state.target=target;
  return (LunaCubeSubmenu){.context=&state,.begin=defaultsSubmenuBegin,
      .draw=defaultsSubmenuDraw,.input=defaultsSubmenuInput};
}
