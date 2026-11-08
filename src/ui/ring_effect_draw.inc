/* Procedural orb texture and an optional burst matching the interior color. */
static u32 ringOrbPixels[32*32] __attribute__((aligned(128)));
static u32 ringBurstPixels[32*32] __attribute__((aligned(128)));
static u32 ringDarkCorePixels[32*32] __attribute__((aligned(128)));
static GSTEXTURE ringOrbTexture;
static GSTEXTURE ringBurstTexture;
static GSTEXTURE ringDarkCoreTexture;
static void ringOrbTextureInit(GSTEXTURE *texture,u32 *pixels,int burst) {
  memset(texture,0,sizeof(*texture));
  texture->Width=texture->Height=32;
  texture->PSM=GS_PSM_CT32;
  texture->Mem=pixels;
  texture->Filter=GS_FILTER_LINEAR;
  texture->Delayed=GS_SETTING_ON;
  for(int y=0;y<32;y++) for(int x=0;x<32;x++) {
    float dx=(x-15.5f)/15.5f,dy=(y-15.5f)/15.5f,r=sqrtf(dx*dx+dy*dy);
    RingOrbLight profile=burst?ringBurstProfile(r):ringOrbProfile(r);
    int alpha=(int)(128*profile.alpha);
    if(burst==2) {
      float core=ringClamp((.28f-r)/.20f);
      alpha=(int)(128*core*core);
    }
    int red=(int)(255*profile.tint.x),green=(int)(255*profile.tint.y),blue=(int)(255*profile.tint.z);
    pixels[y*32+x]=(u32)red|((u32)green<<8)|((u32)blue<<16)|((u32)alpha<<24);
  }
}
static void ringEffectsInit(void) {
  ringRibbonInit();
  ringOrbTextureInit(&ringOrbTexture,ringOrbPixels,0);
  if(ringOrbInteriorColorPreset!=LUNA_RING_ORIGINAL)
    ringOrbTextureInit(&ringBurstTexture,ringBurstPixels,1);
  if(ringOrbInteriorColorPreset==LUNA_RING_INTERIOR_DARK)
    ringOrbTextureInit(&ringDarkCoreTexture,ringDarkCorePixels,2);
}
static void ringEffectsClose(void) {
  /* The queued texture upload must finish before its manager entry is freed. */
  gsKit_queue_exec(gsGlobal); gsKit_finish(); dmaKit_wait_fast();
  gsKit_TexManager_free(gsGlobal,&ringOrbTexture);
  if(ringBurstTexture.Mem)gsKit_TexManager_free(gsGlobal,&ringBurstTexture);
  if(ringDarkCoreTexture.Mem)gsKit_TexManager_free(gsGlobal,&ringDarkCoreTexture);
  ringRibbonClose();
  memset(&ringOrbTexture,0,sizeof(ringOrbTexture));
  memset(&ringBurstTexture,0,sizeof(ringBurstTexture));
  memset(&ringDarkCoreTexture,0,sizeof(ringDarkCoreTexture));
}
static void ringOrbTintTexture(RingVec world,float diameter,float alpha,RingVec tint,GSTEXTURE *texture) {
  if(alpha<=0 || diameter<=0) return;
  RingPoint p=ringWorld(world.x,world.y,world.z);
  if(!p.visible) return;
  float radius=fminf(360,diameter*p.scale*.5f);
  if(p.x+radius<0||p.x-radius>640||p.y+radius<0||p.y-radius>448) return;
  ringOpacity=alpha*lunaUIConfig.start_orb_brightness_percent*.01f;
  int dark=ringOrbInteriorColorPreset==LUNA_RING_INTERIOR_DARK;
  /* Stacked Crimson bursts must retain red instead of clipping to pink/white. */
  int crimsonBurst=ringOrbInteriorColorPreset==LUNA_RING_CRIMSON&&texture==&ringBurstTexture;
  if((dark&&texture==&ringBurstTexture)||crimsonBurst)
    gsKit_set_primalpha(gsGlobal,GS_SETREG_ALPHA(0,1,0,1,0),0);
  /* Use source-alpha modulation with an additive destination for light. */
  gsKit_prim_sprite_texture(gsGlobal,texture,
      ringSX(p.x-radius),ringSY(p.y-radius*448/480),0,0,
      ringSX(p.x+radius),ringSY(p.y+radius*448/480),32,32,3,ringColor(128*tint.x,128*tint.y,128*tint.z,128));
  /* Alpha blending keeps the dark gray center from adding light underneath. */
  if(dark&&texture==&ringOrbTexture&&bindTextureSafe(gsGlobal,&ringDarkCoreTexture)) {
    gsKit_set_primalpha(gsGlobal,GS_SETREG_ALPHA(0,1,0,1,0),0);
    gsKit_prim_sprite_texture(gsGlobal,&ringDarkCoreTexture,
        ringSX(p.x-radius),ringSY(p.y-radius*448/480),0,0,
        ringSX(p.x+radius),ringSY(p.y+radius*448/480),32,32,3,ringColor(128,128,128,128));
  }
  if(dark||crimsonBurst)gsKit_set_primalpha(gsGlobal,GS_SETREG_ALPHA(0,2,0,1,0),0);
}
static void ringOrbTint(RingVec world,float diameter,float alpha,RingVec tint) {
  ringOrbTintTexture(world,diameter,alpha,tint,&ringOrbTexture);
}
static void ringRipple(const RingWave *wave) {
  float age=wave->age,duration=wave->life;
  if(!wave->active) return;
  /* Mode2's native slower path includes sqrt; exact R5900 rounding unresolved. */
  float progress=sqrtf(ringClamp(age/duration));
  float diameter=wave->size*(1+(wave->factor-1)*progress);
  ringRibbonDraw(wave->center,wave->u,wave->v,diameter,(1-age/duration)*wave->alpha*lunaUIConfig.start_orb_brightness_percent*.01f,wave->size>=500,&ringRibbonTexture);
}
static void ringDrawEffects(RingMenu *menu) {
  int bound=bindTextureSafe(gsGlobal,&ringOrbTexture);
  int burstBound=menu->showEntryBurst&&ringBurstTexture.Mem&&bindTextureSafe(gsGlobal,&ringBurstTexture);
  gsKit_set_primalpha(gsGlobal,GS_SETREG_ALPHA(0,2,0,1,0),0);
  if(bound) {
    /* Type2: three satellites begin at -2 rad and converge to each label's
     * pivot over 500 ms, with 0/100/400 ms delay and 800 ms alpha ramps. */
    for(int item=0;item<RING_ITEM_COUNT;item++) {
      for(int orb=0;orb<4;orb++) {
        RingOrbLight light=ringChargeLight(menu->effectAge,orb);
        RingVec world=menu->burstDone?menu->chargePosition[item][orb]:ringItemWorld(menu,item,orb<3?-2*(1-menu->chargeProgress[orb]):0);
        ringOrbTint(world,light.size,light.alpha,light.tint);
      }
    }
    if(menu->moveEffect) {
      RingOrbLight source=ringSelectionLight(menu->moveAge);
      for(int item=0;item<RING_ITEM_COUNT;item++)
        ringOrbTint(ringItemWorld(menu,item,0),source.size,source.alpha,source.tint);
    }
    for(int j=0;j<RING_PARTICLE_COUNT;j++) {
      RingParticle *p=&menu->particles[j];
      if(!p->active || (p->burst&&!menu->showEntryBurst)) continue;
      int coloredBurst=p->burst&&ringOrbInteriorColorPreset!=LUNA_RING_ORIGINAL;
      if(coloredBurst&&!burstBound)continue;
      float age=p->age*.001f;
      RingVec world={p->position.x+p->velocity.x*age,p->position.y+p->velocity.y*age,p->position.z+p->velocity.z*age};
      ringOrbTintTexture(world,p->size,p->alpha*(1-p->age/p->life)*(p->burst?lunaUIConfig.start_burst_brightness_percent*.01f:1),p->tint,
          coloredBurst?&ringBurstTexture:&ringOrbTexture);
    }
    if(menu->bulletStarted) {
      RingOrbLight light=ringBulletLight(menu);
      ringOrbTint(menu->bulletPosition,light.size,light.alpha*(1-ringClamp(menu->exit/500)),light.tint);
    }
  }
  /* Independent scene planes preserve the orientation and position at birth. */
  for(int j=0;j<RING_WAVE_COUNT;j++) ringRipple(&menu->waves[j]);
  gsKit_set_primalpha(gsGlobal,GS_SETREG_ALPHA(0,1,0,1,0),0);
}
