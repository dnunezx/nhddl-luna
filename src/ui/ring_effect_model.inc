/* Static-source effect model. No interpreter, original pixels or runtime code.
 * main:0x9288 Type2, 0x204c4 entry pulses, 0x28d2c cycling trails. */
static RingVec ringTransform(float x,float y,float z,const RingMenu *menu,int decoration) {
  float turn=ringEase(menu->exit/1000);
  float rx=-1.15f+.15f*turn,ry=-.9f+1.4f*turn;
  if(decoration) {
    float a=.04f*cosf(menu->phase),b=.02f*sinf(menu->phase);
    float yy=y*cosf(a)-z*sinf(a),zz=y*sinf(a)+z*cosf(a);
    z=-x*sinf(b)+zz*cosf(b); x=x*cosf(b)+zz*sinf(b); y=yy;
  }
  float yy=y*cosf(rx)-z*sinf(rx),zz=y*sinf(rx)+z*cosf(rx);
  float xx=x*cosf(ry)+zz*sinf(ry);
  z=-x*sinf(ry)+zz*cosf(ry);
  float departure=ringClamp((menu->exit-1000)/1000);
  return (RingVec){xx+140*(1-departure),yy+10*(1-departure),z-160+760*departure};
}
static RingVec ringItemWorld(const RingMenu *menu,int item,float offset) {
  float angle=menu->angle[item]+offset;
  return ringTransform(-250*sinf(angle),250*cosf(angle),0,menu,0);
}
static float ringRandom(RingMenu *menu) {
  menu->random=menu->random*1664525U+1013904223U;
  return (menu->random>>8)/16777216.0f;
}
static RingVec ringSubtract(RingVec a,RingVec b) {
  return (RingVec){a.x-b.x,a.y-b.y,a.z-b.z};
}
static RingOrbLight ringChargeLight(float age,int orb) {
  float fade=1-ringClamp((age-1150)/2000);
  if(orb<3) {
    float delay=(float[]){0,100,400}[orb];
    /* A replacement AnimColor starts at the color present at the flash. */
    return (RingOrbLight){150,ringClamp((fminf(age,1150)-delay)/800)*fade,{1,1,1}};
  }
  float size=age<1150?150+150*ringClamp((age-850)/300):300*fade;
  return (RingOrbLight){size,ringClamp((age-700)/300)*fade,{1,1,1}};
}
static RingOrbLight ringBulletLight(const RingMenu *menu) {
  float red=ringClamp((menu->bulletSinceHit-10000)/1000);
  float fade=menu->bulletDying?ringClamp((menu->bulletSinceHit-20000)/2000):0;
  float size=30;
  if(menu->bulletPulsing) {
    float t=fmodf(menu->bulletPulseAge,400)/200;
    size+=20*(t<1?t:2-t);
    red*=1-ringClamp((menu->bulletPulseAge-800)/1000);
  }
  return (RingOrbLight){size,1-fade,{1,(1-.6f*red)*(1-fade),(1-.6f*red)*(1-fade)}};
}
static RingOrbLight ringSelectionLight(float age) {
  /* 0x288fc: the visible size-250 source fades over 1000 ms. */
  return (RingOrbLight){250,1-ringClamp(age/1000),{1,1,1}};
}
static RingOrbLight ringOrbProfile(float radius) {
  /* Authored radial light: full-alpha violet interior with a narrow white
   * center. The old broad white cone washed out the overlapping flash. */
  float halo=ringClamp((1-radius)/.68f);
  float core=ringClamp((.28f-radius)/.20f); core*=core;
  RingVec tint={(89+166*core)/255,(99+156*core)/255,(179+76*core)/255};
  if(ringEffectsColorPreset!=LUNA_RING_ORIGINAL) {
    static const int palette[LUNA_RING_COLOR_COUNT][3]={
      {0,0,0},{45,232,255},{55,123,255},{176,110,255},
      {255,105,174},{90,242,135},{245,250,255}};
    const int *color=palette[ringEffectsColorPreset];
    tint=(RingVec){(color[0]+(255-color[0])*core)/255,
                  (color[1]+(255-color[1])*core)/255,
                  (color[2]+(255-color[2])*core)/255};
  }
  return (RingOrbLight){0,halo*halo,tint};
}
static void ringWave(RingMenu *menu,RingVec center,int plane,float size,float factor,float life,float alpha,float age) {
  RingVec u={1,0,0},v={0,1,0};
  if(plane) {
    RingVec origin=ringTransform(0,0,0,menu,plane==2);
    u=ringSubtract(ringTransform(1,0,0,menu,plane==2),origin);
    v=ringSubtract(ringTransform(0,1,0,menu,plane==2),origin);
  }
  RingWave *wave=&menu->waves[menu->waveCursor];
  menu->waveCursor=(menu->waveCursor+1)%RING_WAVE_COUNT;
  *wave=(RingWave){center,u,v,age,life,size,factor,alpha,age<life};
}
static void ringBulletTarget(RingMenu *menu,int source) {
  /* 0x2c494 chooses any menu item except the source. */
  menu->bulletTarget=(source+1+(int)(ringRandom(menu)*(RING_ITEM_COUNT-1)))%RING_ITEM_COUNT;
}
static void ringParticle(RingMenu *menu,RingVec p,RingVec v,float size,float life,float alpha) {
  RingParticle *particle=&menu->particles[menu->particleCursor];
  menu->particleCursor=(menu->particleCursor+1)%RING_PARTICLE_COUNT;
  *particle=(RingParticle){p,v,{1,1,1},0,life,size,alpha,1};
}
static void ringBulletBegin(RingMenu *menu,int source) {
  ringBulletTarget(menu,source);
  menu->bulletFrom=menu->bulletPosition;
  menu->bulletTo=ringItemWorld(menu,menu->bulletTarget,0);
  menu->bulletAge=menu->bulletSinceHit=menu->bulletPulseAge=0;
  menu->bulletDuration=2000;
  menu->bulletTrail=250; /* The native timer emits its first sample immediately. */
  menu->bulletPulsing=menu->bulletDying=menu->bulletDeathWaves=0;
}
static void ringBulletStep(RingMenu *menu,float elapsed) {
  if(!menu->bulletStarted) {
    menu->bulletStarted=1;
    menu->bulletPosition=ringItemWorld(menu,menu->selected,0);
    ringBulletBegin(menu,menu->selected);
    return;
  }
  menu->bulletSinceHit+=elapsed;
  if(menu->bulletPulsing) {
    menu->bulletPulseAge+=elapsed;
    if(menu->bulletPulseAge>=2000) ringBulletBegin(menu,menu->bulletTarget);
    return;
  }
  if(menu->bulletDying || menu->bulletSinceHit>=20000) {
    menu->bulletDying=1;
    static const float at[]={20000,20100,20300};
    while(menu->bulletDeathWaves<3 && menu->bulletSinceHit>=at[menu->bulletDeathWaves]) {
      float age=menu->bulletSinceHit-at[menu->bulletDeathWaves++];
      ringWave(menu,menu->bulletPosition,0,100,2,1000,1,age);
    }
    return;
  }
  menu->bulletAge+=elapsed;
  float t=ringClamp(menu->bulletAge/menu->bulletDuration);
  RingVec delta=ringSubtract(menu->bulletTo,menu->bulletFrom);
  menu->bulletPosition=(RingVec){menu->bulletFrom.x+delta.x*t,menu->bulletFrom.y+delta.y*t,menu->bulletFrom.z+delta.z*t};
  RingOrbLight light=ringBulletLight(menu);
  menu->bulletTrail+=elapsed;
  while(menu->bulletTrail>=250) {
    menu->bulletTrail-=250;
    /* User styling: keep the moving orb, without miniature orb copies. */
    if(menu->bulletSinceHit>10000) {
      float z=2*ringRandom(menu)-1,a=RING_TAU*ringRandom(menu),r=sqrtf(fmaxf(0,1-z*z));
      int index=menu->particleCursor;
      ringParticle(menu,menu->bulletPosition,(RingVec){20*r*cosf(a)/.3f,20*r*sinf(a)/.3f,20*z/.3f},20,200,1);
      menu->particles[index].tint=light.tint;
    }
  }
  RingVec target=ringItemWorld(menu,menu->bulletTarget,0);
  RingVec distance=ringSubtract(target,menu->bulletPosition);
  if(distance.x*distance.x+distance.y*distance.y+distance.z*distance.z<25) {
    ringWave(menu,menu->bulletPosition,1,100,4,1000,1,0);
    if(menu->bulletSinceHit>10000) {
      menu->bulletPulsing=1;
      menu->bulletPulseAge=0;
    } else ringBulletBegin(menu,menu->bulletTarget);
  } else if(menu->bulletAge>=2500) {
    /* Keep the source position continuous on a missed target. Native retries
     * use Hermite movement history; this port retains linear retry legs. */
    menu->bulletFrom=menu->bulletPosition;
    menu->bulletTo=target;
    menu->bulletAge=0;
    menu->bulletDuration=menu->bulletSinceHit>10000?1500:2000;
  }
}
static void ringBurst(RingMenu *menu,int item) {
  RingVec p=ringItemWorld(menu,item,0);
  /* 0x9e80: 20 particles, random rotated radius 3000, size 300,
   * linear movement/fade parameter 1000 ms. Direction RNG is LUNA's. */
  for(int j=0;j<20;j++) {
    float z=2*ringRandom(menu)-1,a=RING_TAU*ringRandom(menu);
    float radius=sqrtf(fmaxf(0,1-z*z));
    ringParticle(menu,p,(RingVec){3000*radius*cosf(a),3000*radius*sinf(a),3000*z},300,1000,1);
  }
}
static void ringChargeTick(RingMenu *menu,int orb,float age) {
  static const float delay[3]={0,100,400};
  /* Exponent progress evaluated on reference ticks, 500 ms parameter. */
  if(age>delay[orb]) {
    float u=(age-delay[orb])/500,threshold=1/2.5f;
    float phase=u<threshold?.25f+u*.75f/threshold:1;
    menu->chargeProgress[orb]+=(1-menu->chargeProgress[orb])*(1/(25*.33333298564f+2.20000004768f))*phase;
  }
}
static void ringEffectsStep(RingMenu *menu,float elapsed) {
  menu->effectAge=fminf(10000,menu->effectAge+elapsed);
  menu->moveAge=fminf(10000,menu->moveAge+elapsed);
  menu->pulseAge=fminf(10000,menu->pulseAge+elapsed);
  for(int j=0;j<RING_WAVE_COUNT;j++) {
    RingWave *w=&menu->waves[j];
    if(w->active && (w->age+=elapsed)>=w->life) w->active=0;
  }
  for(int j=0;j<RING_PARTICLE_COUNT;j++) {
    RingParticle *p=&menu->particles[j];
    if(!p->active) continue;
    p->age+=elapsed;
    if(p->age>=p->life) p->active=0;
  }
  /* Catch up only bounded effect ticks; long modal gaps pause in ringStep. */
  if(!menu->closing) {
    static const float entryAt[3]={1250,1350,1550};
    static const float moveAt[3]={1800,2000,2100};
    while(menu->entryWaves<3 && menu->effectAge>=entryAt[menu->entryWaves]) {
      float age=menu->effectAge-entryAt[menu->entryWaves++];
      for(int i=0;i<RING_ITEM_COUNT;i++) ringWave(menu,ringItemWorld(menu,i,0),1,100,5,2000,1,age);
    }
    while(menu->moveWaves<3 && menu->moveAge>=moveAt[menu->moveWaves]) {
      float age=menu->moveAge-moveAt[menu->moveWaves++];
      for(int i=0;i<RING_ITEM_COUNT;i++) ringWave(menu,ringItemWorld(menu,i,0),1,140,3,1750,1,age);
    }
    for(int k=0;k<3;k++) {
      float delay=(float[]){0,100,400}[k];
      int first=(int)(fmaxf(0,fminf(1150,menu->effectAge-elapsed)-delay)*.06f);
      int last=(int)(fmaxf(0,fminf(1150,menu->effectAge)-delay)*.06f);
      for(int tick=first+1;tick<=last;tick++) ringChargeTick(menu,k,delay+tick*(1000.0f/60));
    }
    if(!menu->burstDone && menu->effectAge>=1150) {
      for(int i=0;i<RING_ITEM_COUNT;i++) for(int orb=0;orb<4;orb++)
        menu->chargePosition[i][orb]=ringItemWorld(menu,i,orb<3?-2*(1-menu->chargeProgress[orb]):0);
      int firstBurst=menu->particleCursor;
      for(int i=0;i<RING_ITEM_COUNT;i++) ringBurst(menu,i);
      float late=menu->effectAge-1150;
      for(int j=0;j<20*RING_ITEM_COUNT;j++) {
        RingParticle *p=&menu->particles[(firstBurst+j)%RING_PARTICLE_COUNT];
        p->age=late;
        if(late>=p->life) p->active=0;
      }
      menu->burstDone=1;
    }
    /* Keep the continuous selection glow; copied trailing orbs are omitted. */
    if(menu->moveAge>=1800) menu->moveEffect=0;
    if(menu->entry>=2550 && menu->moveAge>=1800 && menu->pulseAge>=1000 && ringRandom(menu)<elapsed*.0006f) {
      menu->pulseAge=0;
      ringWave(menu,ringTransform(0,0,0,menu,0),1,500,1.1f,1000,1,0);
    }
    if(menu->entry>=2550) ringBulletStep(menu,elapsed);
  } else {
    /* 0x24870 emits 50 snapshot copies in each exit stage. Reference 60 Hz
     * ticks are used here; native script-yield cadence is still unresolved. */
    int first=(int)((menu->exit-elapsed)*.06f),last=(int)(menu->exit*.06f);
    for(int tick=first+1;tick<=last;tick++) {
      if((tick>=1&&tick<=50)||(tick>=61&&tick<=110))
        ringWave(menu,ringTransform(0,0,0,menu,1),2,500,1,100,1,menu->exit-tick*(1000.0f/60));
    }
  }
}
