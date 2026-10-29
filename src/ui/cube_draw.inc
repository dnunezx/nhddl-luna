#include "cube_glass.inc"
static MenuPromptTransition cubeFooterTransition;
static MenuPromptSnapshot cubeFooterDesired;
static int cubeFooterCollect;
void uiCubeSubmenuFooterDraw(const PromptBar *bar,float opacity) {
  if(cubeBackdropOnly)return;
  if(cubeFooterCollect) {
    cubeFooterDesired.count=bar->count>MENU_PROMPT_MAX?MENU_PROMPT_MAX:bar->count;
    cubeFooterDesired.hasNote=0;
    for(int i=0;i<cubeFooterDesired.count;i++) {
      snprintf(cubeFooterDesired.labels[i],sizeof(cubeFooterDesired.labels[i]),"%s",bar->items[i].label);
      cubeFooterDesired.items[i]=(ButtonPrompt){bar->items[i].icon,cubeFooterDesired.labels[i]};
    }
  } else drawMenuPromptBar(&cubeFooterTransition,(int)cubeSX(32),(int)cubeSY(426),
      (int)cubeSX(608),(int)cubeSY(464),7,HeaderTextColor,*bar,opacity);
}
static void cubeMenuTextScaled(int left,int top,int right,int z,float scale,uint64_t color,
                               const char *text,int rightAligned) {
  if(!text||right<=left)return;
  const char *shown=text;
  char fitted[512];
  size_t length=strcspn(text,"\n");
  if(text[length]||length>=sizeof(fitted)||getLineDrawWidth(text)*scale>right-left) {
    if(getLineDrawWidth("...")*scale>right-left)return;
    size_t limit=length<sizeof(fitted)-4?length:sizeof(fitted)-4;
    int low=0,high=(int)limit;
    size_t best=0;
    while(low<=high) {
      int middle=low+(high-low)/2;
      size_t cut=(size_t)middle;
      while(cut&&((unsigned char)text[cut]&0xc0)==0x80)cut--;
      memcpy(fitted,text,cut);memcpy(fitted+cut,"...",4);
      if(getLineDrawWidth(fitted)*scale<=right-left){best=cut;low=middle+1;}
      else high=middle-1;
    }
    memcpy(fitted,text,best);memcpy(fitted+best,"...",4);shown=fitted;
  }
  int x=rightAligned?right-(int)ceilf(getLineDrawWidth(shown)*scale):left;
  drawTextLineScaledCrisp(x,top,right,z,scale,color,shown);
}
void uiCubeMenuTextDraw(int left,int top,int right,int z,uint64_t color,
                        const char *text,int rightAligned) {
  cubeMenuTextScaled(left,top,right,z,1,color,text,rightAligned);
}
void uiCubeMenuHelpDraw(int left,int right,uint64_t color,const char *text) {
  if(!text||!text[0])return;
  const float scale=.8f;
  int y=(int)cubeSY(390),step=(int)ceilf(getFontLineHeight()*scale);
  char line[512];
  const char *rest=text;
  size_t length=strlen(text),cut=0;
  for(size_t i=0;i<length&&i<sizeof(line)-1;i++) {
    if(text[i]!=' '&&text[i]!='\n')continue;
    memcpy(line,text,i);line[i]=0;
    if(getLineDrawWidth(line)*scale>right-left)break;
    cut=i;
    if(text[i]=='\n')break;
  }
  if(getLineDrawWidth(text)*scale>right-left||strchr(text,'\n')) {
    if(cut) {
      memcpy(line,text,cut);line[cut]=0;
      cubeMenuTextScaled(left,y,right,7,scale,color,line,0);
      rest=text+cut+1;while(*rest==' ')rest++;
      y+=step;
    }
  }
  cubeMenuTextScaled(left,y,right,7,scale,color,rest,0);
}

static void cubeText(float cx,float y,uint64_t color,const char *text) {
  /* Use the active LUNA font at its native size, just like List view. */
  drawText((int)cubeSX(cx)-getLineWidth(text)/2,(int)cubeSY(y),6,
      (int)cubeSX(635),0,color,text);
}
static uint64_t cubeTextFade(uint64_t color,float opacity) {
  unsigned alpha=(unsigned)((color>>24)&255);
  return (color&~(0xffULL<<24))|((uint64_t)(alpha*cubeClamp(opacity))<<24);
}
void uiCubeOptionsFooterDraw(const char *action,float opacity) {
  ButtonPrompt prompts[]={{ICON_CIRCLE,lunaText("Back")},{ICON_CROSS,lunaText(action)}};
  PromptBar bar={NULL,prompts,2};
  drawPromptBarTransition(20,gsGlobal->Height-footerHeight+8,gsGlobal->Width-20,gsGlobal->Height,
      8,FontMainColor,bar,bar,1,opacity);
}
void uiCubeOptionsFooterTransitionDraw(float libraryProgress,float pageProgress) {
  ButtonPrompt library[]={{ICON_CIRCLE,lunaText("View")},{ICON_CROSS,lunaText("Launch")},
      {ICON_START,lunaText("Menu")},{ICON_R1,lunaText("More")}};
  ButtonPrompt hub[]={{ICON_CIRCLE,lunaText("Back")},{ICON_CROSS,lunaText("Open")}};
  ButtonPrompt page[]={{ICON_CIRCLE,lunaText("Back")},{ICON_CROSS,lunaText("Change")}};
  PromptBar from={NULL,library,4},to={NULL,hub,2};
  float progress=cubeClamp(libraryProgress);
  if(progress>=1) {
    from=to;to=(PromptBar){NULL,page,2};progress=pageProgress;
  }
  drawPromptBarTransition(20,gsGlobal->Height-footerHeight+8,gsGlobal->Width-20,gsGlobal->Height,
      8,FontMainColor,from,to,progress,1);
}
void uiCubeSubmenuFocusDraw(int left,int top,int right,int bottom,float opacity) {
  gsKit_prim_sprite(gsGlobal,left,top,right,bottom,6,
      glassPresetColor(47,100,132,(int)(70*opacity)));
  uint64_t accent=getGlassColorPreset()==GLASS_COLOR_BLACK?
      cubeColor(200,200,200,opacity*.75f):glassPresetColor(112,208,255,(int)(96*opacity));
  gsKit_prim_sprite(gsGlobal,left,top,left+2,bottom,7,accent);
}
void uiCubeSubmenuPageDraw(const LunaCubePage *page,int selected,
                          LunaCubePageFocus *focus,float opacity) {
  int left=(int)cubeSX(32),right=(int)cubeSX(506),line=getFontLineHeight();
  int step=(page->stackedValues?2*line:line)+line/3;
  int top=(int)cubeSY(127),bottom=(int)cubeSY(370);
  int visible=(bottom-top)/step;
  if(visible<1)visible=1;
  int first=selected/visible*visible,y=top+(selected-first)*step;
  uint32_t now=uiNowMs();
  unsigned duration=focus->glideMs>0?(unsigned)focus->glideMs:110;
  float progress=cubeClamp((now-focus->startMs)/(float)duration);
  if(focus->selected<0||focus->first!=first) {
    focus->fromY=focus->toY=y*256;focus->selected=selected;focus->first=first;
  } else if(focus->selected!=selected) {
    focus->fromY+=(int)((focus->toY-focus->fromY)*
        lunaNavEase((int)(progress*1000)))/1000;
    focus->toY=y*256;focus->startMs=now;focus->selected=selected;progress=0;
  }
  int focusY=(focus->fromY+(focus->toY-focus->fromY)*
      lunaNavEase((int)(progress*1000))/1000)/256;
  uint64_t ink=cubeTextFade(HeaderTextColor,opacity);
  uint64_t bright=cubeColor(240,250,255,opacity);
  drawTextWindow(left,(int)cubeSY(22),right,(int)cubeSY(22)+line,6,ink,
                 ALIGN_LEFT,lunaText(page->sectionTitle?page->sectionTitle:"System Configuration"));
  /* Subtitle pages need both header lines clear of the pane's top rim. */
  int titleY=(int)cubeSY(50);
  drawTextWindow(left,titleY,right,titleY+line,6,
                 cubeColor(232,232,70,opacity),ALIGN_LEFT,lunaText(page->title));
  if(page->subtitle) {
    uiCubeMenuTextDraw(left,(int)cubeSY(74),right,7,ink,page->subtitle,0);
  }
  /* The expanded crystal supplies the pane's glass and rim. */
  gsKit_prim_sprite(gsGlobal,cubeSX(22),cubeSY(99),cubeSX(518),cubeSY(383),5,
      glassPresetColor(5,13,34,(int)(48*opacity)));
  if(page->count>0)uiCubeSubmenuFocusDraw(left-4,focusY-1,right-10,
      focusY+(page->stackedValues?2*line:line)+2,opacity);
  int maxLabel=0,maxValue=0;
  if(!page->stackedValues&&page->value) {
    for(int row=first;row<page->count&&row<first+visible;row++) {
      const char *label=lunaText(page->label(page->context,row));
      int width=label?(int)ceilf(getLineDrawWidth(label)):0;
      if(width>maxLabel)maxLabel=width;
      const char *value=lunaText(page->value(page->context,row));
      width=value?(int)ceilf(getLineDrawWidth(value)):0;
      if(width>maxValue)maxValue=width;
    }
  }
  /* Equal eight-pixel insets inside the existing focus plate. */
  int textLeft=left+4,textRight=right-18;
  int content=textRight-textLeft;
  int valueSpace=(int)fminf(maxValue,fmaxf(content/2,content-maxLabel-16));
  int valueLeft=textRight-valueSpace;
  int nameRight=valueSpace?valueLeft-16:textRight;
  for(int row=first;row<page->count&&row<first+visible;row++) {
    const char *label=lunaText(page->label(page->context,row));
    const char *value=page->value?lunaText(page->value(page->context,row)):NULL;
    uint64_t color=row==selected?bright:ink;
    int rowY=top+(row-first)*step;
    uiCubeMenuTextDraw(textLeft,rowY,nameRight,7,color,label,0);
    if(value&&value[0]) {
      if(page->stackedValues) {
        uiCubeMenuTextDraw(textLeft+8,rowY+line,textRight,7,color,value,0);
      } else uiCubeMenuTextDraw(valueLeft,rowY,textRight,7,color,value,1);
    }
  }
  if(page->count==0)drawTextWindow(textLeft,top,textRight,top+line,7,ink,
                                  ALIGN_LEFT,lunaText("Nothing to show"));
  if(opacity>=.999f)drawListScrollbar(right-6,top,top+visible*step,
                                     page->count,first,visible,7);
  const char *description=page->description&&page->count>0?
      page->description(page->context,selected):page->status;
  uiCubeMenuHelpDraw(left,(int)cubeSX(608),ink,lunaText(description));
  if(!page->libraryFooter) {
    ButtonPrompt prompts[]={{ICON_DPAD,lunaText("Navigate")},
        {ICON_CROSS,lunaText(page->adjust?"Change":page->value?"Select":"Open")},{ICON_CIRCLE,lunaText("Back")}};
    uiCubeSubmenuFooterDraw(&(PromptBar){NULL,prompts,3},opacity);
  }
}
static int cubeDraw(CubeMenu *m,const char *status,int confirming,const LunaCubeHub *hub,const LunaCubeSubmenu *submenu) {
  const char *const *labels=hub->labels;
  gsKit_clear(gsGlobal,BGColor);gsKit_TexManager_nextFrame(gsGlobal);
  GSTEST savedTest=*gsGlobal->Test;uint64_t savedBlend=gsGlobal->PrimAlpha;
  int savedAlpha=gsGlobal->PrimAlphaEnable,savedPabe=gsGlobal->PABE;
  gsKit_set_test(gsGlobal,GS_ZTEST_OFF);
  int sceneReady=1;
  if(hub->background)sceneReady=hub->background(hub->context,m->closing);
  else drawSharedLibraryBackground(uiNowMs());
  gsKit_set_test(gsGlobal,GS_ZTEST_OFF);gsKit_set_test(gsGlobal,GS_ATEST_OFF);
  gsGlobal->PrimAlphaEnable=GS_SETTING_ON;
  gsKit_set_primalpha(gsGlobal,GS_SETREG_ALPHA(0,1,0,1,0),0);
  float entry=cubeClamp(m->entry/500);
  float overall=entry*(m->entering?1-cubeClamp((m->exit-1140)/300):1-cubeClamp(m->exit/500));
  float text=overall*(m->entering?1-cubeClamp(m->exit/200):1);
  if(m->submenuOpen)text*=1-cubeSmooth(m->submenuAge/(CUBE_SUBMENU_MS*.6f));
  CubeVec camera={0,0,0};
  float submenuProgress=m->submenuOpen?m->submenuAge/CUBE_SUBMENU_MS:0;
  if(m->entering) {
    /* LUNA easing adaptation. The native two-manager blend is not yet
     * recovered; preserve its 1200/1440 ms camera/exit timing landmarks. */
    float t=cubeClamp(m->exit/1200),u=1-powf(1-t,3);
    camera=(CubeVec){m->cameraTarget.x*u,m->cameraTarget.y*u,m->cameraTarget.z*u};
  }
  cubeSceneScale=1;
  cubeSceneOffsetX=cubeSceneOffsetY=0;
  CubeGlass scene[CUBE_COUNT];int order[CUBE_COUNT];
  for(int i=0;i<m->itemCount;i++) {
    float fade=overall;
    if(m->submenuOpen&&i!=m->selected%CUBE_COUNT)fade*=1-cubeSmooth(submenuProgress);
    if(m->entering&&i!=m->selected%CUBE_COUNT)fade*=1-cubeClamp(m->exit/1000);
    cubeGlassBuild(&scene[i],i,m,camera,fade);order[i]=i;
  }
  for(int i=1;i<m->itemCount;i++) {
    int index=order[i],j=i;
    while(j>0&&scene[order[j-1]].depth<scene[index].depth){order[j]=order[j-1];j--;}
    order[j]=index;
  }
  for(int i=0;i<m->itemCount;i++) {
    if(m->submenuOpen&&order[i]==m->selected%CUBE_COUNT)continue;
    cubeGlassDraw(&scene[order[i]],camera);
  }
  if(m->submenuOpen)cubeGlassDraw(&scene[m->selected%CUBE_COUNT],camera);
  cubeRestoreGlassState();
  drawLunaLogo(cubeSX(60),cubeSY(20),cubeSX(78),6,cubeColor(185,210,235,text));
  uiCubeMenuTextDraw((int)cubeSX(60),(int)cubeSY(128),(int)cubeSX(310),6,
      cubeColor(232,232,70,text),lunaText(hub->title),0);
  if(status)cubeText(320,53,cubeColor(215,229,244,text),status);
  if(hub->subtitle)drawTextWindow((int)cubeSX(25),(int)cubeSY(65),(int)cubeSX(615),
      (int)cubeSY(112),6,cubeColor(215,229,244,text),ALIGN_HCENTER,hub->subtitle);
  const int listTop=(int)cubeSY(167);
  const int rowStep=getFontLineHeight()+getFontLineHeight()/3;
  const int listLeft=(int)cubeSX(60),rowRight=(int)cubeSX(300);
  int visible=(int)((cubeSY(352)-listTop)/rowStep);
  if(visible<1)visible=1;
  if(visible>m->itemCount)visible=m->itemCount;
  int first=m->selected>=visible?m->selected-visible+1:0;
  const char *selectedLabel=lunaText(labels[m->selected]);
  const int selectedLeft=listLeft;
  int textRight=selectedLeft+(int)ceilf(getLineDrawWidth(selectedLabel));
  if(textRight>rowRight-12)textRight=rowRight-12;
  if(text>.01f)drawPSBBNFocusGlow(selectedLeft,(int)(listTop+rowStep*fmaxf(0,fminf(visible-1,m->row-first))),rowRight,textRight);
  for(int i=first;i<first+visible;i++) {
    float alpha=text*(i==m->selected?1:.65f);
    const char *label=lunaText(labels[i]);
    uint64_t color=i==m->selected?GS_SETREG_RGBA(0xF0,0xFA,0xFF,0x80):HeaderTextColor;
    uiCubeMenuTextDraw(listLeft,listTop+rowStep*(i-first),rowRight-12,6,
        cubeTextFade(color,alpha),label,0);
  }
  ButtonPrompt prompts[]={{ICON_DPAD,lunaText("Navigate")},{ICON_CROSS,lunaText("Open")},
      {ICON_CIRCLE,lunaText("Back")}};
  ButtonPrompt confirm[]={{ICON_CROSS,lunaText("Confirm")},{ICON_CIRCLE,lunaText("Cancel")}};
  if(confirming&&hub->confirmText)
    cubeText(175,365,cubeColor(240,250,255,text),lunaText(hub->confirmText));
  else if(hub->description) {
    const char *description=hub->description(hub->context,m->selected);
    uiCubeMenuHelpDraw((int)cubeSX(32),(int)cubeSX(608),cubeColor(190,211,232,text),lunaText(description));
  }
  cubeFooterCollect=!hub->libraryFooter&&!cubeBackdropOnly;
  if(cubeFooterCollect)uiCubeSubmenuFooterDraw(
      &(PromptBar){NULL,confirming?confirm:prompts,confirming?2:3},overall);
  if(m->submenuOpen&&submenu) {
    float opacity=cubeSmooth((m->submenuAge/CUBE_SUBMENU_MS-.28f)/.72f);
    if(opacity>0)submenu->draw(submenu->context,opacity);
  }
  if(cubeFooterCollect)drawMenuPromptBar(&cubeFooterTransition,(int)cubeSX(32),(int)cubeSY(426),
      (int)cubeSX(608),(int)cubeSY(464),7,HeaderTextColor,
      (PromptBar){NULL,cubeFooterDesired.items,cubeFooterDesired.count},overall);
  cubeFooterCollect=0;
  // One footer owns the whole handoff, independent of the scene's text fades.
  if(hub->libraryFooter && !cubeBackdropOnly) {
    float pageProgress=cubeSmooth((submenuProgress-.28f)/.72f);
    if(hub->footer)hub->footer(hub->context,pageProgress);
    else uiCubeOptionsFooterTransitionDraw(overall,pageProgress);
  }
  cubeSceneScale=1;cubeSceneOffsetX=cubeSceneOffsetY=0;
  gsKit_set_primalpha(gsGlobal,savedBlend,savedPabe);gsGlobal->PrimAlphaEnable=savedAlpha;
  *gsGlobal->Test=savedTest;gsKit_set_test(gsGlobal,GS_ZTEST_ON);
  return sceneReady;
}
