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
#include "ui/neo.h"
#include "options.h"
#include <libpad.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum { AP_BACKGROUND,AP_VIEWS,AP_COLORS,AP_EFFECTS,AP_TEXT,AP_SOUND,AP_THEMES,AP_CATEGORY_COUNT };
static const char *const categories[]={"Backgrounds","Library Views","Interface Colors",
  "Background Effects","Text","Ambient Sound","Themes"};
static const char *const backgrounds[]={"Stars & Cubes","Ambient Orbs","Red Clouds","Midnight Cubes",
  "System Configuration","Neo","Aurora","Ronin","Torch","Tempest","Space Flight","Underground","Static-X","Phantom"};
static const struct {
  const char *name;
  LibraryBackground background;
  GlassColorPreset glass;
  LunaRingColor ring,exterior,interior;
} appearanceThemes[] = {
  {"LUNA", LIBRARY_BACKGROUND_STARS, GLASS_COLOR_ORIGINAL,
    LUNA_RING_ORIGINAL,LUNA_RING_ORIGINAL,LUNA_RING_ORIGINAL},
  {"Underground", LIBRARY_BACKGROUND_UNDERGROUND, GLASS_COLOR_UNDERGROUND,
    LUNA_RING_CRIMSON,LUNA_RING_CRIMSON,LUNA_RING_INTERIOR_DARK},
  {"Static-X", LIBRARY_BACKGROUND_STATIC_X, GLASS_COLOR_STATIC_X,
    LUNA_RING_BLUE,LUNA_RING_BLUE,LUNA_RING_INTERIOR_DARK},
  {"Phantom", LIBRARY_BACKGROUND_PHANTOM, GLASS_COLOR_PHANTOM,
    LUNA_RING_WHITE,LUNA_RING_WHITE,LUNA_RING_INTERIOR_DARK}
};
#define AP_THEME_COUNT ((int)(sizeof(appearanceThemes)/sizeof(*appearanceThemes)))
static int appearanceThemeIndex(const LunaAppearance *a) {
  for(int i=0;i<AP_THEME_COUNT;i++)
    if(a->background==appearanceThemes[i].background&&a->glassColor==appearanceThemes[i].glass)return i;
  return -1;
}
static int appearanceResetThemeIndex(const LunaAppearance *a) {
  int theme=appearanceThemeIndex(a);
  if(theme>=0)return theme;
  // A custom background can still retain its theme's interface palette.
  for(int i=1;i<AP_THEME_COUNT;i++)
    if(a->glassColor==appearanceThemes[i].glass)return i;
  for(int i=1;i<AP_THEME_COUNT;i++)
    if(a->background==appearanceThemes[i].background)return i;
  return 0;
}
static void appearanceApplyTheme(LunaAppearance *a,int theme) {
  a->background=appearanceThemes[theme].background;
  a->glassColor=appearanceThemes[theme].glass;
  a->ringColor=appearanceThemes[theme].ring;
  a->ringEffectsColor=appearanceThemes[theme].exterior;
  a->ringOrbInteriorColor=appearanceThemes[theme].interior;
}
static const char *const colors[]={"Original","Cyan","Violet","Rose","Green","Gold","White"};
static const char *const shapes[]={"Diamond","Cube","Octahedron","Sphere","LUNA","Skull","Atom","Iron Cross"};
typedef struct {
  Target *target;
  LunaAppearance saved,pending,runtime;
  int category,loadError,theme;
  uint32_t savedAt;
  const char *message;
  char value[96];
  int rows[AP_CATEGORY_COUNT];
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
  uiRingSetOrbInteriorColor((LunaRingColor)a->ringOrbInteriorColor);
  setClassicArtOverlap(a->classicOverlap);setPs1CaseStyle((Ps1CaseStyle)a->ps1CaseStyle);
  setCollectionGameList(a->collectionGameList);ambientSetEnabled(a->ambientSound);
  if(a->orbsTheme!=old->orbsTheme)setAmbientOrbsTheme((AmbientOrbsTheme)a->orbsTheme,uiNowMs());
  if(a->orbShapes!=old->orbShapes)setAmbientOrbsShapes(a->orbShapes,uiNowMs());
  setAmbientOrbsColor(ORBS_COLOR_PART_ORBS,(AmbientOrbsColor)a->orbsColor);
  setAmbientOrbsColor(ORBS_COLOR_PART_TAILS,(AmbientOrbsColor)a->tailsColor);
  lunaSpaceSetSpeed(a->spaceSpeed);lunaSpaceSetDepth(a->spaceDepth);
  setNeoAppearance(a->neoSpeed,a->neoDensity,a->neoColor);
  s->runtime=*a;return 0;
}
static const char *appearanceHubDescription(void *context,int row) {
  AppearanceEditor *s=context;
  if(s->message)return s->message;
  static const char *const descriptions[]={
    "Preview the background used throughout LUNA.","Choose library views and cover layouts.",
    "Choose the glass tint and Start-menu ring color.","Tune Ambient Orbs, Neo, or Space Flight.",
    "Choose the interface font. Wide menu headings stay unchanged.","Play ambient music while browsing.",
    "Apply or reset a coordinated background, interface, ring and orb palette."};
  return descriptions[row];
}
static int appearanceRowCount(AppearanceEditor *s) {
  if(s->category==AP_THEMES)return 2;
  if(s->category==AP_VIEWS)return UI_VIEW_COUNT+3;
  if(s->category==AP_COLORS)return 4;
  if(s->category==AP_EFFECTS&&s->pending.background==LIBRARY_BACKGROUND_NEO)return 3;
  if(s->category==AP_EFFECTS)return s->pending.background==LIBRARY_BACKGROUND_SPACE_FLIGHT?2:4+ORB_SHAPE_PLAYTIME;
  return 1;
}
static const char *appearanceRowLabel(void *context,int row) {
  AppearanceEditor *s=context;
  switch(s->category) {
    case AP_THEMES:return row?"Reset theme":"Theme";
    case AP_BACKGROUND:return "Background";
    case AP_COLORS:return (const char *const[]){"Glass color","Ring color","Menu exterior glow","Menu interior glow"}[row];
    case AP_TEXT:return "Font";
    case AP_SOUND:return "Ambient sound";
    case AP_VIEWS:
      if(row<UI_VIEW_COUNT)return lunaNavViewLabel(lunaViewCycleOrder[row]);
      return (const char *const[]){"Cover and disc layout","PS1 case style","Collections game list"}[row-UI_VIEW_COUNT];
    default:
      if(s->pending.background==LIBRARY_BACKGROUND_NEO)return (const char *const[]){"Speed","Density","Color"}[row];
      if(s->pending.background==LIBRARY_BACKGROUND_SPACE_FLIGHT)return row?"Depth":"Speed";
      if(row<4)return (const char *const[]){"Behavior","Appearance","Orb color","Tail color"}[row];
      return shapes[row-4];
  }
}
static const char *appearanceRowValue(void *context,int row) {
  AppearanceEditor *s=context;LunaAppearance *a=&s->pending;int value=0;
  switch(s->category) {
    case AP_THEMES: {
      if(row)return appearanceThemes[s->theme].name;
      const int theme=appearanceThemeIndex(a);
      return theme<0?"Custom":appearanceThemes[theme].name;
    }
    case AP_BACKGROUND:return backgrounds[a->background];
    case AP_COLORS:return row?
      (const char *const[]){"Original","Cyan","Blue","Violet","Rose","Green","White","Crimson","Dark"}[row==1?a->ringColor:row==2?a->ringEffectsColor:a->ringOrbInteriorColor]:
      (const char *const[]){"Original","Luminous","Cosmic","Cyber","Underground","Static-X","Phantom"}[a->glassColor];
    case AP_TEXT:return a->font==UI_FONT_PSBBN?"PSBBN":"DejaVu";
    case AP_SOUND:value=a->ambientSound;break;
    case AP_VIEWS:
      if(row<UI_VIEW_COUNT)value=(a->enabledViews>>lunaViewCycleOrder[row])&1;
      else if(row==UI_VIEW_COUNT)return a->classicOverlap?"Overlap":"Separate";
      else if(row==UI_VIEW_COUNT+1)return a->ps1CaseStyle==PS1_CASE_SQUARE?"Square":"Tall";
      else value=a->collectionGameList;
      break;
    default:
      if(a->background==LIBRARY_BACKGROUND_NEO) {
        if(row==0)return (const char *const[]){"Slow","Normal","Fast"}[a->neoSpeed];
        if(row==1)return (const char *const[]){"Sparse","Normal","Dense"}[a->neoDensity];
        return (const char *const[]){"Green","Cyan","Blue","Violet","Red","Amber","White"}[a->neoColor];
      }
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
  if(s->category==AP_THEMES)return row?
    "Press X to restore this theme's background, glass, ring and orb colors.":
    "Choose a matching background, glass, ring and orb palette. Changes save automatically.";
  if(s->category==AP_COLORS&&row==1)return "Colors the Start-menu ring and ripples.";
  if(s->category==AP_COLORS&&row==2)return "Colors the Start-menu orbs' outer glow.";
  if(s->category==AP_COLORS&&row==3)return "Colors the Start-menu orbs' inner glow and explosions.";
  if(s->category==AP_EFFECTS) {
    if(s->pending.background==LIBRARY_BACKGROUND_NEO)return (const char *const[]){
      "Change how quickly Neo's characters fall. Normal keeps the original speed.",
      "Choose sparse, normal, or dense rain. Normal keeps the original column count.",
      "Choose Neo's rain color. Green keeps its original appearance."}[row];
    if(s->pending.background==LIBRARY_BACKGROUND_SPACE_FLIGHT)return "Changes save automatically.";
    if(s->pending.background!=LIBRARY_BACKGROUND_ORBS)return "Choose Ambient Orbs, Neo, or Space Flight under Backgrounds.";
    if(row>=4)return "Choose LUNA formations. All off keeps playtime.";
    if(row>=2&&s->pending.orbsAppearance==ORBS_APPEARANCE_PS2_ORIGINAL)return "PS2 original appearance uses its original colors.";
  }
  return "Changes save automatically.";
}
static void appearanceAdjust(void *context,int row,int direction) {
  AppearanceEditor *s=context;s->message=NULL;
  if(s->category==AP_THEMES&&row==1&&direction!=0)return;
  if(s->loadError) {
    LunaAppearance recovered;
    s->savedAt=0;
    s->loadError=loadAppearance(s->target,&recovered);
    if(s->loadError) {s->message="Could not read appearance settings";return;}
    s->saved=s->pending=recovered;
    s->theme=appearanceResetThemeIndex(&recovered);
  }
  LunaAppearance candidate=s->pending;
  int selectedTheme=s->theme;
  switch(s->category) {
    case AP_THEMES: {
      if(!row) {
        int theme=appearanceThemeIndex(&candidate);
        selectedTheme=theme<0?(direction<0?AP_THEME_COUNT-1:1):(theme+AP_THEME_COUNT+direction)%AP_THEME_COUNT;
      }
      appearanceApplyTheme(&candidate,selectedTheme);
      break;
    }
    case AP_BACKGROUND:candidate.background=(candidate.background+LIBRARY_BACKGROUND_COUNT+direction)%LIBRARY_BACKGROUND_COUNT;break;
    case AP_COLORS:
      if(row==3)candidate.ringOrbInteriorColor=(candidate.ringOrbInteriorColor+LUNA_RING_INTERIOR_COLOR_COUNT+direction)%LUNA_RING_INTERIOR_COLOR_COUNT;
      else if(row==2)candidate.ringEffectsColor=(candidate.ringEffectsColor+LUNA_RING_COLOR_COUNT+direction)%LUNA_RING_COLOR_COUNT;
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
      if(candidate.background==LIBRARY_BACKGROUND_NEO) {
        uint32_t *value=row==0?&candidate.neoSpeed:row==1?&candidate.neoDensity:&candidate.neoColor;
        unsigned count=row==0?NEO_SPEED_COUNT:row==1?NEO_DENSITY_COUNT:NEO_COLOR_COUNT;
        *value=(*value+count+direction)%count;
        break;
      }
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
  s->theme=selectedTheme;
  s->savedAt=uiNowMs();
}
#include "appearance_workspace.inc"
int uiAppearanceLoop(Target *target) {
  AppearanceEditor state={.target=target};
  // The file browser or another application may have removed the settings.
  resetAppearanceCache();
  state.loadError=loadAppearance(target,&state.saved);
  state.pending=state.runtime=state.saved;
  state.theme=appearanceResetThemeIndex(&state.saved);
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
