/* One 4 KB procedural texture shared by all orbs and trails. Alpha/hue follow
 * the inspected reference's soft violet halo/white core; pixels are authored. */
static u32 ringOrbPixels[32*32] __attribute__((aligned(128)));
static GSTEXTURE ringOrbTexture;
static void ringEffectsInit(void) {
  ringRibbonInit();
  memset(&ringOrbTexture,0,sizeof(ringOrbTexture));
  ringOrbTexture.Width=ringOrbTexture.Height=32;
  ringOrbTexture.PSM=GS_PSM_CT32;
  ringOrbTexture.Mem=ringOrbPixels;
  ringOrbTexture.Filter=GS_FILTER_LINEAR;
  ringOrbTexture.Delayed=GS_SETTING_ON;
  for(int y=0;y<32;y++) for(int x=0;x<32;x++) {
    float dx=(x-15.5f)/15.5f,dy=(y-15.5f)/15.5f,r=sqrtf(dx*dx+dy*dy);
    RingOrbLight profile=ringOrbProfile(r);
    int alpha=(int)(128*profile.alpha);
    int red=(int)(255*profile.tint.x),green=(int)(255*profile.tint.y),blue=(int)(255*profile.tint.z);
    ringOrbPixels[y*32+x]=(u32)red|((u32)green<<8)|((u32)blue<<16)|((u32)alpha<<24);
  }
}
static void ringEffectsClose(void) {
  /* The queued texture upload must finish before its manager entry is freed. */
  gsKit_queue_exec(gsGlobal); gsKit_finish(); dmaKit_wait_fast();
  gsKit_TexManager_free(gsGlobal,&ringOrbTexture);
  ringRibbonClose();
  memset(&ringOrbTexture,0,sizeof(ringOrbTexture));
}
static void ringOrbTint(RingVec world,float diameter,float alpha,RingVec tint) {
  if(alpha<=0 || diameter<=0) return;
  RingPoint p=ringWorld(world.x,world.y,world.z);
  if(!p.visible) return;
  float radius=fminf(360,diameter*p.scale*.5f);
  if(p.x+radius<0||p.x-radius>640||p.y+radius<0||p.y-radius>448) return;
  ringOpacity=alpha;
  /* Use source-alpha modulation with an additive destination for light. */
  gsKit_prim_sprite_texture(gsGlobal,&ringOrbTexture,
      ringSX(p.x-radius),ringSY(p.y-radius*448/480),0,0,
      ringSX(p.x+radius),ringSY(p.y+radius*448/480),32,32,3,ringColor(128*tint.x,128*tint.y,128*tint.z,128));
}
static void ringRipple(const RingWave *wave) {
  float age=wave->age,duration=wave->life;
  if(!wave->active) return;
  /* Mode2's native slower path includes sqrt; exact R5900 rounding unresolved. */
  float progress=sqrtf(ringClamp(age/duration));
  float diameter=wave->size*(1+(wave->factor-1)*progress);
  GSTEXTURE *texture=ringEffectsColorPreset==LUNA_RING_ORIGINAL?&ringRibbonTexture:&ringEffectsRibbonTexture;
  ringRibbonDraw(wave->center,wave->u,wave->v,diameter,(1-age/duration)*wave->alpha,wave->size>=500,texture);
}
static void ringDrawEffects(RingMenu *menu) {
  int bound=bindTextureSafe(gsGlobal,&ringOrbTexture);
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
      if(!p->active) continue;
      float age=p->age*.001f;
      RingVec world={p->position.x+p->velocity.x*age,p->position.y+p->velocity.y*age,p->position.z+p->velocity.z*age};
      ringOrbTint(world,p->size,p->alpha*(1-p->age/p->life),p->tint);
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
