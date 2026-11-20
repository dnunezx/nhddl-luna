#include "ui/genres_menu.h"
#include "ui/psnow_genre_model.h"
#include "ui/psnow_genre_render.h"
#include "ui/cube_menu.h"
#include "ui/language.h"
#include "ui/pad.h"
#include "ui/view_internal.h"
#include "ui/ui.h"
#include "genres.h"
#include <libpad.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  PsnowGenreModel model;
  int genres[PSNOW_GENRE_ITEMS],counts[PSNOW_GENRE_ITEMS],count,assign;
  int inGames,assignFromGames,gameSelected;
  int assignGenre,assignSelected,assignHeld;
  LunaCubePageFocus assignFocus;
  uint32_t assignRepeatAt;
  TargetList *library,*games;
  LunaGenreBrowser *state;
  Target *target;
  TargetFilter platform;
  const unsigned char *favorites;
  char status[160];
} GenreMenu;
#include "genre_lettering.inc"

static int matches(const GenreMenu *m,const Target *t) {
  return !(m->platform==TARGET_PS2_ONLY && t->platform!=TARGET_PS2) &&
         !(m->platform==TARGET_PS1_ONLY && t->platform!=TARGET_PS1) &&
         (!m->favorites || m->favorites[t->idx]);
}
static void populate(GenreMenu *m,int selectedGenre) {
  genreLetteringOpen();
  setGenreCoverArt(0);
  int counts[LUNA_GENRE_LIMIT]={0};
  for(int i=0;i<m->library->total;i++) {
    Target *t=getTargetByIdx(m->library,i);
    if(!matches(m,t))continue;
    uint32_t mask=lunaGenreMask(t);
    for(int g=0;g<lunaGenreCount();g++)if(mask&(1U<<g))counts[g]++;
  }
  m->count=0;
  for(int g=0;g<lunaGenreCount();g++) {
    m->genres[m->count]=g;m->counts[m->count++]=counts[g];
  }
  int selected=0;
  for(int i=0;i<m->count;i++)if(m->genres[i]==selectedGenre)selected=i;
  m->state->genre=m->genres[selected];m->state->inGames=m->inGames=0;
  psnowGenreInit(&m->model,m->count,selected,gsGlobal->Mode==GS_MODE_PAL?50:60);
}
static uint64_t faded(uint64_t color,float alpha) {
  return (color&~(255ULL<<24))|((uint64_t)(((color>>24)&255)*alpha)<<24);
}
static const char *genreLabel(int genre) {
  const char *name=lunaGenreName(genre);
  return genre<LUNA_GENRE_DEFAULTS?lunaText(name):name;
}
static int openGames(GenreMenu *m,int genre) {
  if(genre<0 || genre>=lunaGenreCount())return -1;
  genreLetteringClose();
  releasePSBBNCovers();
  setGenreCoverArt(1);
  if(lunaGenreFilter(m->games,m->library,m->platform,m->favorites,genre))return -1;
  m->state->genre=genre;m->state->inGames=m->inGames=1;m->assign=0;
  m->gameSelected=targetViewIndex(m->games,m->state->selectedGame);
  if(m->gameSelected<0)m->gameSelected=0;
  int window=m->games->total<PSBBN_COVER_CACHE_COUNT?m->games->total:PSBBN_COVER_CACHE_COUNT;
  psnowGameInit(&m->model,window,0,gsGlobal->Mode==GS_MODE_PAL?50:60);
  if(m->games->total) {
    m->state->selectedGame=getTargetByIdx(m->games,m->gameSelected);
    setCollectionArtForeground(1);refreshCollectionCovers(m->games,m->gameSelected,-1);
  }
  m->status[0]=0;return 0;
}
static int assignmentRows(void) {
  int line=getFontLineHeight();
  int rows=(gsGlobal->Height*370/480-gsGlobal->Height*127/480)/(line+line/3);
  return rows>0?rows:1;
}
static void openAssignment(GenreMenu *m) {
  genreLetteringClose();
  m->assignFromGames=m->inGames;
  m->assignGenre=m->inGames?m->state->genre:m->genres[m->model.selected];
  Target *selected=m->inGames?m->state->selectedGame:m->target;
  releasePSBBNCovers();setGenreCoverArt(0);setCollectionArtForeground(0);
  uiCubeGenreEnd();
  filterTargetView(m->games,m->library,TARGET_ALL,NULL);
  m->assignSelected=targetViewIndex(m->games,selected);
  if(m->assignSelected<0)m->assignSelected=0;
  m->assignFocus=(LunaCubePageFocus){.selected=-1,.glideMs=110};
  m->assignHeld=0;m->assign=1;m->inGames=0;m->status[0]=0;
}
static const char *assignmentLabel(void *context,int row) {
  GenreMenu *m=context;
  return getTargetByIdx(m->games,row)->name;
}
static const char *assignmentValue(void *context,int row) {
  GenreMenu *m=context;
  return lunaGenreMask(getTargetByIdx(m->games,row))&(1U<<m->assignGenre)?"On":"Off";
}
static void drawAssignment(GenreMenu *m) {
  char subtitle[160];snprintf(subtitle,sizeof(subtitle),"%s  (%d / %d)",
      genreLabel(m->assignGenre),m->games->total?m->assignSelected+1:0,m->games->total);
  uiCubePanelGlassDraw(gsGlobal->Width*22/640,gsGlobal->Height*99/480,
      gsGlobal->Width*518/640,gsGlobal->Height*383/480,4,0);
  LunaCubePage page={.title="Assign",.sectionTitle="Genres",.subtitle=subtitle,
      .count=m->games->total,.label=assignmentLabel,.value=assignmentValue,.context=m,
      .status=m->status[0]?m->status:"Select games to add or remove.",.libraryFooter=1};
  uiCubeSubmenuPageDraw(&page,m->assignSelected,&m->assignFocus,1);
  Target *game=getTargetByIdx(m->games,m->assignSelected);
  int assigned=game && (lunaGenreMask(game)&(1U<<m->assignGenre));
  const ButtonPrompt prompts[]={{ICON_CROSS,lunaText(assigned?"Remove":"Add")},
                               {ICON_CIRCLE,lunaText("Done")}};
  uiCubeSubmenuFooterDraw(&(PromptBar){NULL,prompts,2},1);
}
static void centerText(float cx,float cy,float nativeSize,uint64_t color,const char *text) {
  float scale=nativeSize*gsGlobal->Height/(480.0f*getFontLineHeight());
  float width=getLineWidth(text)*scale;
  int x=(int)(cx*gsGlobal->Width/640-width*.5f);
  int y=(int)(cy*gsGlobal->Height/480-nativeSize*gsGlobal->Height/960);
  if(y<0 || y>=gsGlobal->Height || x>=gsGlobal->Width || x+width<0)return;
  drawTextLineScaledCrisp(x,y,gsGlobal->Width-20,6,scale,color,text);
}
static void drawGenreCount(const GenreMenu *m,int count) {
  char detail[64];
  snprintf(detail,sizeof(detail),lunaText(count==1?"%d game":"%d games"),count);
  const int layout=setAuthoredTextLayout(0);
  float width=getLineDrawWidth(detail);
  float sx=gsGlobal->Width/640.0f,sy=gsGlobal->Height/480.0f;
  float cx=320-10*500.0f/750,cy=240+120*500.0f/750;
  float age=m->model.descriptionAge;
  /* Match native menu descriptions: no glyph enlargement or added padding. */
  drawTextLineNative((int)roundf(cx*sx-width*.5f),
      (int)roundf(cy*sy-getFontLineHeight()*.5f),gsGlobal->Width-20,6,
      faded(HeaderTextColor,psnowDescriptionAlpha(age)),detail);
  setAuthoredTextLayout(layout);
  psnowDescriptionDraw(&m->model,cx,cy,fmaxf(90,(width*.5f+14)/sx),age);
}
static void drawGames(GenreMenu *m) {
  if(!m->games->total) {
    centerText(320,240,24,HeaderTextColor,lunaText("No games assigned to this genre."));return;
  }
  serviceCollectionCovers(m->games,m->gameSelected);
  int order[PSNOW_GENRE_ITEMS];PsnowVec positions[PSNOW_GENRE_ITEMS];
  for(int i=0;i<m->model.count;i++){order[i]=i;positions[i]=psnowGenrePosition(&m->model,i);}
  for(int i=1;i<m->model.count;i++) {
    int next=order[i],j=i;
    while(j && positions[order[j-1]].z>positions[next].z){order[j]=order[j-1];j--;}
    order[j]=next;
  }
  for(int j=0;j<m->model.count;j++) {
    int i=order[j];PsnowGenreItem *item=&m->model.items[i];PsnowVec p=positions[i];
    if(item->current.alpha<=.01f || 600-p.z<64)continue;
    int relative=(i-m->model.selected+m->model.count)%m->model.count;
    if(relative>m->model.count/2)relative-=m->model.count;
    int cache=PSBBN_COVER_CACHE_FOCUS+relative;
    if(cache<0 || cache>=PSBBN_COVER_CACHE_COUNT)continue;
    GSTEXTURE *texture=psbbnCoverTextures[cache];
    int w=256,h=256;getCollectionCoverSourceSize(cache,&w,&h);
    float scale=500/(600-p.z),sx=gsGlobal->Width/640.0f,sy=gsGlobal->Height/480.0f;
    float left=(320+(p.x-w*.5f)*scale)*sx,right=(320+(p.x+w*.5f)*scale)*sx;
    float top=(240-(p.y+h*.5f)*scale)*sy,bottom=(240-(p.y-h*.5f)*scale)*sy;
    float alpha=item->current.alpha*fminf(1,m->model.entryAge/1000);
    if(!prepareCollectionCoverTexture(cache) ||
       !psnowGameCoverDraw(texture,left,top,right,bottom,item->current.shade,alpha))
      gsKit_prim_sprite(gsGlobal,left,top,right,bottom,4,faded(ColorPanel,alpha));
  }
  Target *selected=getTargetByIdx(m->games,m->gameSelected);m->state->selectedGame=selected;
  /* genre 0x29080 restores title rows to (320,330/350/370). */
  centerText(320,330,20,faded(FontMainColor,psnowDescriptionAlpha(m->model.descriptionAge)),selected->name);
  float titleHalfWidth=fmaxf(70,getLineWidth(selected->name)*10.0f/getFontLineHeight()+12);
  titleHalfWidth=fminf(290,titleHalfWidth);
  psnowDescriptionDraw(&m->model,320,330,titleHalfWidth,m->model.descriptionAge);
  char count[64];snprintf(count,sizeof(count),"%d / %d",m->gameSelected+1,m->games->total);
  centerText(320,350,20,HeaderTextColor,count);
}
static void draw(GenreMenu *m) {
  gsKit_TexManager_nextFrame(gsGlobal);gsKit_clear(gsGlobal,BGColor);
  drawSharedLibraryBackground(uiNowMs());
  gsKit_set_test(gsGlobal,GS_ZTEST_OFF);
  if(m->assign){drawAssignment(m);goto present;}
  psnowRingDraw(&m->model);
  if(m->inGames){drawGames(m);goto footer;}
  int order[PSNOW_GENRE_ITEMS];PsnowVec positions[PSNOW_GENRE_ITEMS];
  for(int i=0;i<m->count;i++) {order[i]=i;positions[i]=psnowGenreCubePosition(&m->model,i);}
  for(int i=1;i<m->count;i++) {
    int next=order[i],j=i;
    while(j && positions[order[j-1]].z>positions[next].z){order[j]=order[j-1];j--;}
    order[j]=next;
  }
  LunaGenreCrystal crystals[PSNOW_GENRE_ITEMS];
  for(int j=0;j<m->count;j++) {
    int i=order[j];PsnowVec p=positions[i];PsnowGenreItem *item=&m->model.items[i];
    float alpha=psnowGenreCubeAlpha(&m->model);
    /* AddCubeMenu: crystal offset is (0,100,0) in world space. */
    crystals[j]=(LunaGenreCrystal){p.x,p.y+100,p.z,item->rotation[0],item->rotation[1],
        item->rotation[2],alpha,i==m->model.selected,item->color[0],item->color[1],item->color[2],
        psnowCrystalRefraction(m->model.entryAge),psnowCrystalRefraction(m->model.entryAge)};
  }
  uiCubeGenreDraw(crystals,m->count,psnowGenreProjectionScale(&m->model));
  for(int j=0;j<m->count;j++) {
    int i=order[j];PsnowVec p=positions[i];
    float alpha=psnowGenreLabelAlpha(&m->model,i),depth=600-p.z;
    if(alpha<=.01f || depth<64)continue;
    float scale=500*psnowGenreProjectionScale(&m->model)/depth;
    float cx=320+p.x*scale,cy=240-p.y*scale;
    const char *label=lunaText(lunaGenreName(m->genres[i]));
    /* Original front-facing labels retain their projected anchors. Fit only
     * viewport edges and the library footer to the active output/font. */
    uint64_t color=i==m->model.selected?GS_SETREG_RGBA(255,255,0,128):FontMainColor;
    if(!genreLetteringDraw(m->genres[i],cx,cy,28,faded(color,alpha)))
      centerText(cx,cy,28,faded(color,alpha),label);
    if(i==m->model.selected)drawGenreCount(m,m->counts[i]);
  }
footer:
  if(m->inGames) {
    char heading[160];snprintf(heading,sizeof(heading),"%s / %s",
        lunaText("Genres"),genreLabel(m->state->genre));
    drawTextLineNative(24,18,gsGlobal->Width-24,7,FontMainColor,heading);
  }else
  drawTextLineNative(24,18,gsGlobal->Width-24,7,FontMainColor,
      lunaText("Genres"));
  if(m->status[0])drawTextWindow(24,gsGlobal->Height-footerHeight-60,
      gsGlobal->Width-24,gsGlobal->Height-footerHeight-12,7,WarnTextColor,ALIGN_HCENTER,m->status);
  const ButtonPrompt browse[]={{ICON_CROSS,lunaText(m->inGames?"Launch":"Open")},{ICON_TRIANGLE,lunaText("Assign")},
                               {ICON_CIRCLE,lunaText(m->inGames?"Genres":"Back")}};
  drawPromptBar(20,gsGlobal->Height-footerHeight+8,gsGlobal->Width-20,gsGlobal->Height,
      8,FontMainColor,(PromptBar){NULL,browse,3});
present:
  gsKit_set_test(gsGlobal,GS_ZTEST_ON);
  gsKit_queue_exec(gsGlobal);gsKit_finish();gsKit_sync_flip(gsGlobal);usleep(1000);
}
int uiGenresLoop(TargetList *library,Target *target,TargetFilter platform,
                 const unsigned char *favorites,LunaGenreBrowser *state,Target **launchTarget) {
  if(launchTarget)*launchTarget=NULL;
  if(!library || !target || !state || !launchTarget)return LUNA_GENRE_BROWSER_BACK;
  static GenreMenu menu;
  memset(&menu,0,sizeof(menu));menu.library=library;menu.target=target;
  menu.platform=platform;menu.favorites=favorites;menu.state=state;
  menu.games=createTargetView(library);if(!menu.games)return LUNA_GENRE_BROWSER_BACK;
  uiCubeGenreBegin();
  int ringError=psnowRingOpen();
  if(!state->inGames || openGames(&menu,state->genre))populate(&menu,state->genre);
  if(ringError)snprintf(menu.status,sizeof(menu.status),"%s",lunaText("Could not load the genre ring."));
  GSTEST savedTest=*gsGlobal->Test;
  uint64_t savedBlend=gsGlobal->PrimAlpha;
  int savedAlpha=gsGlobal->PrimAlphaEnable,savedPabe=gsGlobal->PABE;
  int previous=pollInput(),result=LUNA_GENRE_BROWSER_BACK;
  uint32_t last=uiNowMs();
  for(;;) {
    uint32_t now=uiNowMs();psnowGenreStep(&menu.model,now-last);last=now;
    draw(&menu);
    int held=pollInput(),pressed=held&~previous;previous=held;
    if(!menu.assign && menu.model.entryAge<1000)continue;
    if(pressed&PAD_CIRCLE) {
      if(menu.assign) {
        uiCubeGenreBegin();
        menu.assign=0;
        if(menu.assignFromGames)openGames(&menu,menu.assignGenre);
        else populate(&menu,menu.assignGenre);
      }else if(menu.inGames) {
        releasePSBBNCovers();setCollectionArtForeground(0);
        menu.inGames=state->inGames=0;populate(&menu,state->genre);
      }else break;
      continue;
    }
    if((pressed&PAD_TRIANGLE) && !menu.assign) {
      openAssignment(&menu);continue;
    }
    if(menu.assign) {
      int directions=held&(PAD_UP|PAD_DOWN|PAD_LEFT|PAD_RIGHT);
      if(directions!=menu.assignHeld) {
        menu.assignHeld=directions;menu.assignRepeatAt=now+350;
      }else if(directions && (int32_t)(now-menu.assignRepeatAt)>=0) {
        pressed|=directions;menu.assignRepeatAt=now+100;
      }
      int offset=pressed&PAD_UP?-1:pressed&PAD_DOWN?1:
          pressed&PAD_LEFT?-assignmentRows():pressed&PAD_RIGHT?assignmentRows():0;
      if(offset && menu.games->total) {
        menu.assignSelected=lunaNavWrap(menu.games->total,menu.assignSelected+offset);
        menu.status[0]=0;
      }else if((pressed&PAD_CROSS) && menu.games->total) {
        Target *game=getTargetByIdx(menu.games,menu.assignSelected);
        uint32_t mask=lunaGenreMask(game)^(1U<<menu.assignGenre);
        int error=lunaGenreSet(game,mask);
        snprintf(menu.status,sizeof(menu.status),"%s",lunaText(error?
            "Could not save genres. Check the game drive.":"Saved"));
      }
      continue;
    }
    int direction=pressed&(PAD_LEFT|PAD_UP)?-1:pressed&(PAD_RIGHT|PAD_DOWN)?1:0;
    if(direction) {
      psnowGenreMove(&menu.model,direction);
      if(!menu.inGames)state->genre=menu.genres[menu.model.selected];
      if(menu.inGames && menu.games->total>1) {
        int previousGame=menu.gameSelected;
        menu.gameSelected=lunaNavWrap(menu.games->total,menu.gameSelected+direction);
        refreshCollectionCovers(menu.games,menu.gameSelected,previousGame);
      }
    }else if(pressed&PAD_CROSS) {
      if(menu.inGames) {
        if(menu.games->total) {
          *launchTarget=getTargetByIdx(menu.games,menu.gameSelected);state->selectedGame=*launchTarget;
          result=LUNA_GENRE_BROWSER_LAUNCH;break;
        }
        continue;
      }
      int genre=menu.genres[menu.model.selected];
      openGames(&menu,genre);
    }
  }
  releasePSBBNCovers();setGenreCoverArt(0);setCollectionArtForeground(0);freeTargetList(menu.games);
  genreLetteringClose();psnowRingClose();uiCubeGenreEnd();
  gsKit_set_primalpha(gsGlobal,savedBlend,savedPabe);gsGlobal->PrimAlphaEnable=savedAlpha;
  *gsGlobal->Test=savedTest;
  gsKit_set_test(gsGlobal,savedTest.ATE?GS_ATEST_ON:GS_ATEST_OFF);
  return result;
}
