#include "ui/animation.h"
/* Numeric port of PS Now's RingMenuScript, separate from the Start menu.
 * nowhot.amx: 0x21698/0x21ef0/0x221a0 layout, 0x1d224 pivot placement,
 * 0x22440 entry, 0x26ab8 angle motion, 0x2b484 move_type 1.
 * Original data slots 8508..8648. Native degree/radian conversion is retained.
 * LUNA binds the scene to its renderer and substitutes its own materials. */
#include "ui/psnow_genre_model.h"
#include <math.h>
#include <string.h>
#define PI 3.14159265358979323846f
#define TAU (2*PI)
static float clamp(float x) { return fminf(1,fmaxf(0,x)); }

PsnowSlot psnowGenreSlot(int index,int count) {
  static const float angle[]={-3.05f,-2.65f,-2,-1,0,1.7f,1.9f,2,2.8f};
  static const float y[]={520,350,500,500,300,600,600,600,300};
  static const float z[]={100,-90,-90,-90,-90,-90,-90,-90,-90};
  static const float alpha[]={1,1,1,0,0,0,0,0,1};
  int slot=index,hidden=0;
  if(count==4 && index==3)slot=4;
  if(count==5 && index>=3)slot=index+1;
  if(count>=8) {
    if(index==count-1)slot=8;
    else if(index==count-2)slot=7;
    else if(index>=6){slot=6;hidden=1;}
  }
  if(count<1 || index<0 || index>=count || slot>8)return (PsnowSlot){0};
  return (PsnowSlot){angle[slot]+(hidden?.01f*(index-6):0),y[slot],z[slot],
      alpha[slot],!hidden,1};
}

float psnowGenreProjectionScale(const PsnowGenreModel *m) {
  (void)m;return 1;
}
float psnowDescriptionRight(float age) {
  static const float target[]={1,-.9f,-.2f,-.9f,-.2f,-.9f,1};
  if(!lunaAnimationBlink())return 1;
  if(age<0 || age>=600)return 1;
  int stage=(int)(age/100);
  return target[stage]+(target[stage+1]-target[stage])*(age-stage*100)/100;
}
float psnowDescriptionAlpha(float age) {
  if(!lunaAnimationBlink())return 1;
  if(age<600)return 0;
  if(age>=1000)return 1;
  float phase=fmodf(age-600,80)/40;
  return phase<1?1-phase:phase-1;
}
float psnowSmallWaveScale(float age) {
  float progress=clamp(age/1000);
  return 1+2*progress*progress;
}
float psnowSmallWaveAge(const PsnowGenreModel *m,int index) {
  static const float delay[]={1000,1200,1300};
  if(m->profile!=1 || index<0 || index>=3 || m->selectionAge<delay[index])return -1;
  float age=m->selectionAge-delay[index];
  return age<1000?age:-1;
}
float psnowCrystalRefraction(float entryAge) {
  // FIN type 1 starts AnimRefractionMain after its 1000 ms wait.
  // Its 2000 ms linear timer multiplies RFF0/RFF1, then snaps at .97.
  float progress=clamp((entryAge-1000)/2000);
  return progress>=.97f?1:progress;
}

/* genre.amx 0x2313c -> 0x23664; data 8764,8820,8844,8868,8940.
 * The selected image and its neighbors use six uneven slots, not a wheel. */
PsnowSlot psnowGameSlot(int index,int count) {
  static const float angle[]={-3.14f,-2.7f,-2.4f,0,2.4f,2.7f};
  static const float y[]={0,-300,-200,500,-200,-300};
  static const float z[]={80,50,50,0,-50,0};
  static const float shade[]={1,.5f,.2f,0,.2f,.5f};
  int slot=index;
  if(count==3 && index==2)slot=5;
  else if(count==4 && index==3)slot=5;
  else if(count==5 && index>=3)slot=index+1;
  else if(count>=6) {
    if(index==count-1)slot=5;
    else if(index==count-2)slot=4;
    else if(index>=3)slot=3;
  }
  if(count<1 || index<0 || index>=count || slot>5)return (PsnowSlot){0};
  return (PsnowSlot){angle[slot],y[slot],z[slot],slot==3?0:1,1,shade[slot]};
}

float psnowGenreExponent(float progress,float age,float duration,int fps) {
  int frames=(int)(duration*fps*.001f);
  static const float shortCoefficient[]={1,.9615384936f,.78125f,.636942625f,.5291005373f};
  float coefficient=frames<5?shortCoefficient[frames<0?0:frames]:
      1/((frames-5)*.33333298563957214f+2.200000047683716f);
  float seconds=duration*.001f,time=age/duration;
  float threshold=1/(seconds*3+1),base=fminf(seconds*.5f,1);
  float phase=time<threshold?base+time*(1-base)/threshold:1;
  return clamp(progress+(1-progress)*coefficient*phase);
}
static unsigned nextRandom(unsigned *r) { *r=*r*1664525U+1013904223U;return *r; }
void psnowGenreInit(PsnowGenreModel *m,int count,int selected,int fps) {
  memset(m,0,sizeof(*m));
  m->count=count>PSNOW_GENRE_ITEMS?PSNOW_GENRE_ITEMS:count;
  m->selected=selected>=0&&selected<m->count?selected:0;m->fps=fps==50?50:60;
  m->profile=1;
  m->descriptionAge=-1000;
  m->selectionAge=-1;
  m->cubePhase=m->cubeFrom=m->cubeTarget=m->selected;m->cubeProgress=1;
  unsigned seed=0x4c554e41;
  for(int i=0;i<m->count;i++) {
    PsnowGenreItem *p=&m->items[i];
    p->current=p->from=p->target=psnowGenreSlot((i-m->selected+m->count)%m->count,m->count);
    p->age=0;p->boost=1;
    /* AddCubeMenu: random initial radians, .001*random(1000) orientation,
     * and .00001*random(1000) per script-update angular velocities. */
    for(int a=0;a<3;a++) {
      p->rotation[a]=.001f*(nextRandom(&seed)%1000);
      p->speed[a]=.00001f*(nextRandom(&seed)%1000);
      p->color[a]=p->colorFrom[a]=1;
    }
  }
}
void psnowGameInit(PsnowGenreModel *m,int count,int selected,int fps) {
  psnowGenreInit(m,count,selected,fps);m->profile=2;
  for(int i=0;i<m->count;i++)
    m->items[i].current=m->items[i].from=m->items[i].target=
        psnowGameSlot((i-m->selected+m->count)%m->count,m->count);
}
void psnowGenreMove(PsnowGenreModel *m,int direction) {
  if(m->count<2)return;
  direction=direction<0?-1:1;
  m->selected=(m->selected+direction+m->count)%m->count;
  m->descriptionAge=-1000;m->selectionAge=0;
  if(m->profile==1) {
    m->cubeFrom=m->cubePhase;m->cubeTarget+=direction;
    m->cubeAge=m->cubeProgress=0;
  }
  for(int i=0;i<m->count;i++) {
    PsnowGenreItem *p=&m->items[i];p->from=p->current;
    memcpy(p->colorFrom,p->color,sizeof(p->color));
    int slot=(i-m->selected+m->count)%m->count;
    p->target=m->profile==2?psnowGameSlot(slot,m->count):psnowGenreSlot(slot,m->count);
    p->current.visible=p->target.visible;
    float delta=p->target.angle-p->current.angle;
    if(direction>0) { while(delta>0)delta-=TAU; }
    else { while(delta<0)delta+=TAU; }
    p->target.angle=p->current.angle+delta;
    p->angleTarget=fabsf(delta)>PI?p->current.angle+.5f*delta:p->target.angle;
    p->stage=fabsf(delta)>PI?1:2;
    p->angleDuration=p->stage==1?(m->profile==2?350:300):500;
    p->angleAge=p->angleProgress=p->age=0;
  }
  m->items[m->selected].boost=15;
  if(lunaAnimationInstant(LUNA_ANIM_MENU_MOVE)) {
    m->cubePhase=m->cubeTarget;m->cubeProgress=1;m->descriptionAge=10000;m->selectionAge=10000;
    for(int i=0;i<m->count;i++) {
      PsnowGenreItem *p=&m->items[i];p->current=p->target;p->stage=0;
      p->age=10000;p->angleProgress=1;
      for(int a=0;a<3;a++)p->color[a]=a==2?(i==m->selected?1:.1f):0;
    }
  }
}
void psnowGenreStep(PsnowGenreModel *m,unsigned elapsedMs) {
  if(elapsedMs>250)return;
  float tick=1000.0f/m->fps;
  if(lunaAnimationInstant(LUNA_ANIM_MENU_ENTRY)) {
    m->entryAge=10000;m->entryProgress=m->entryRotation=1;
  } else {
    m->entryTicks+=lunaAnimationStep(LUNA_ANIM_MENU_ENTRY,elapsedMs,10000);
    while(m->entryTicks>=tick) {
      m->entryTicks-=tick;m->entryAge+=tick;
      m->entryProgress=psnowGenreExponent(m->entryProgress,m->entryAge,1000,m->fps);
      if(m->profile==2 && m->entryAge>500)
        m->entryRotation=psnowGenreExponent(m->entryRotation,m->entryAge-500,1000,m->fps);
    }
  }
  if(lunaAnimationInstant(LUNA_ANIM_MENU_MOVE) || (!lunaAnimationBlink() && m->descriptionAge>=0))m->descriptionAge=10000;
  m->ticks+=lunaAnimationInstant(LUNA_ANIM_MENU_MOVE)?elapsedMs:
      lunaAnimationStep(LUNA_ANIM_MENU_MOVE,elapsedMs,10000);
  while(m->ticks>=tick) {
    m->ticks-=tick;
    m->descriptionAge+=tick;
    if(m->selectionAge>=0)m->selectionAge+=tick;
    if(m->profile==1 && m->cubeProgress<1) {
      m->cubeAge+=tick;
      m->cubeProgress=psnowGenreExponent(m->cubeProgress,m->cubeAge,500,m->fps);
      if(m->cubeProgress>=.998f)m->cubeProgress=1;
      m->cubePhase=m->cubeFrom+(m->cubeTarget-m->cubeFrom)*m->cubeProgress;
    }

    m->ringVelocity=fminf(.02f,m->ringVelocity+.001f);
    if(lunaAnimationEffects()>0 && lunaAnimationBlink())m->ringPhase+=m->ringVelocity;
    for(int i=0;i<m->count;i++) {
      PsnowGenreItem *p=&m->items[i];p->age+=tick;
      if(p->stage) {
        p->angleAge+=tick;
        p->angleProgress=p->stage==1?clamp(p->angleAge/p->angleDuration):
            psnowGenreExponent(p->angleProgress,p->angleAge,p->angleDuration,m->fps);
        p->current.angle=p->from.angle+(p->angleTarget-p->from.angle)*p->angleProgress;
        if(p->angleProgress>=.998f) {
          p->current.angle=p->angleTarget;
          if(p->stage==1) {
            p->from.angle=p->angleTarget;p->angleTarget=p->target.angle;
            p->angleAge=p->angleProgress=0;p->angleDuration=200;p->stage=2;
          }else p->stage=0;
        }
      }
      /* Position manager is 700 ms/mode 6; color is 500 ms/linear. */
      p->current.y+= (p->target.y-p->current.y)*
          (psnowGenreExponent(0,p->age,700,m->fps));
      p->current.z+= (p->target.z-p->current.z)*
          (psnowGenreExponent(0,p->age,700,m->fps));
      p->current.alpha=p->from.alpha+(p->target.alpha-p->from.alpha)*clamp(p->age/500);
      p->current.shade=p->from.shade+(p->target.shade-p->from.shade)*clamp(p->age/500);
      /* FIN/MOVE type 1: after the 1000 ms timer, animate each crystal
       * toward (0,0,1) selected or (0,0,.1) otherwise, over 500 ms. */
      if(m->profile==1)for(int a=0;a<3;a++) {
        float target=a==2?(i==m->selected?1:.1f):0;
        p->color[a]=p->colorFrom[a]+(target-p->colorFrom[a])*clamp((p->age-1000)/500);
      }
      if(lunaAnimationEffects()>0)for(int a=0;a<3;a++)p->rotation[a]+=p->speed[a]*p->boost;
      p->boost=fmaxf(1,p->boost-.1f);
    }
  }
}
static PsnowVec rotate(PsnowVec p,float x,float y,float z) {
  /* SetRot(Object, ..., 0): the inspected native composes Ry * Rx * Rz
   * at 0x713ff0 and 0x71409c, so column vectors encounter Z, X, then Y. */
  PsnowVec q={p.x*cosf(z)-p.y*sinf(z),p.x*sinf(z)+p.y*cosf(z),p.z};
  p=(PsnowVec){q.x,q.y*cosf(x)-q.z*sinf(x),q.y*sinf(x)+q.z*cosf(x)};
  return (PsnowVec){p.x*cosf(y)+p.z*sinf(y),p.y,-p.x*sinf(y)+p.z*cosf(y)};
}
PsnowVec psnowGenreTransform(const PsnowGenreModel *m,PsnowVec p,int decoration) {
  float t=1-m->entryProgress;
  if(decoration)p=rotate(p,.04f*cosf(m->ringPhase),.02f*sinf(m->ringPhase),0);
  if(m->profile==2) {
    /* genre 0x238d0: move from (-100,1000,-2500), and rotate from
     * authored X+0.5 with a separate 500 ms delay. */
    p=rotate(p,-1.4f+.5f*(1-m->entryRotation),-.3f,.3f);
    p.x-=100*t;p.y+=80+920*t;p.z-=1000+1500*t;
  }else {
    p=rotate(p,-1.3f+PI*t,-.5f+PI*t,.4f+PI*t);
    p.x+=3000*t;p.y+=150;p.z-=1200+6000*t;
  }
  return p;
}
PsnowVec psnowGenrePosition(const PsnowGenreModel *m,int index) {
  const PsnowSlot *s=&m->items[index].current;
  float radius=m->profile==2?1000:750;
  return psnowGenreTransform(m,(PsnowVec){-(radius+s->y)*sinf(s->angle),
      (radius+s->y)*cosf(s->angle),s->z},0);
}
PsnowVec psnowGenreCubePosition(const PsnowGenreModel *m,int index) {
  if(m->profile!=1 || m->count<3)return psnowGenrePosition(m,index);
  /* Preserve the channel's three foreground anchors and perspective sizes.
   * Only distribute the formerly hidden/stacked rear slots along the ring.
   * Its texture peak is at radius 2400 * 151 / 320 = 1132.5 scene units. */
  float slot=fmodf(index-m->cubePhase,(float)m->count);
  if(slot<0)slot+=m->count;
  PsnowSlot front=psnowGenreSlot(0,m->count),right=psnowGenreSlot(1,m->count);
  PsnowSlot left=psnowGenreSlot(m->count-1,m->count);
  float angle,radius,z,rear=0;
  if(slot<1) {
    angle=front.angle+(right.angle-front.angle)*slot;
    radius=750+front.y+(right.y-front.y)*slot;
    z=front.z+(right.z-front.z)*slot;
  }else if(slot>m->count-1) {
    float t=slot-(m->count-1);
    angle=left.angle+(front.angle+TAU-left.angle)*t;
    radius=750+left.y+(front.y-left.y)*t;
    z=left.z+(front.z-left.z)*t;
  }else {
    float t=(slot-1)/(m->count-2);
    angle=right.angle+(left.angle-right.angle)*t;
    float edge=fminf(slot-1,m->count-1-slot);
    rear=clamp(edge);
    float anchor=750+right.y+(left.y-right.y)*t;
    float depth=right.z+(left.z-right.z)*t;
    radius=anchor+(1132.5f-anchor)*rear;
    z=depth*(1-rear);
  }
  PsnowVec local={-radius*sinf(angle),radius*cosf(angle),z};
  PsnowVec p=psnowGenreTransform(m,local,0);
  if(rear>0) {
    PsnowVec wobble=psnowGenreTransform(m,local,1);
    p.x+=(wobble.x-p.x)*rear;p.y+=(wobble.y-p.y)*rear;p.z+=(wobble.z-p.z)*rear;
  }
  return p;
}
float psnowGenreCubeAlpha(const PsnowGenreModel *m) {
  // FIN's crystals appear after its 1000 ms wait, then fade over 500 ms.
  return clamp((m->entryAge-1000)/500);
}
float psnowGenreLabelAlpha(const PsnowGenreModel *m,int index) {
  if(m->count<=0)return 0;
  float distance=fabsf(remainderf(index-m->cubePhase,(float)m->count));
  // Only the three front slots carry captions. Fade at the edge while turning.
  return (1-clamp((distance-1)/.2f))*clamp(m->entryAge/1000);
}
