#ifndef LUNA_PSNOW_GENRE_MODEL_H
#define LUNA_PSNOW_GENRE_MODEL_H

#define PSNOW_GENRE_ITEMS 34
typedef struct { float x,y,z; } PsnowVec;
typedef struct { float angle,y,z,alpha; int visible; float shade; } PsnowSlot;
typedef struct {
  PsnowSlot current,from,target;
  float angleTarget,angleProgress,angleAge,angleDuration,age;
  float rotation[3],speed[3],boost;
  float color[3],colorFrom[3];
  int stage;
} PsnowGenreItem;
typedef struct {
  PsnowGenreItem items[PSNOW_GENRE_ITEMS];
  int count,selected,fps,profile;
  float ticks,entryProgress,entryAge,entryRotation,ringPhase,ringVelocity;
  float descriptionAge,selectionAge,entryTicks;
  float cubePhase,cubeFrom,cubeTarget,cubeProgress,cubeAge;
} PsnowGenreModel;
/* Translated numeric routines of PS Now RingMenuScript, move/fin type 1.
 * Renderer and library bindings are supplied by LUNA. No VM required. */
PsnowSlot psnowGenreSlot(int index,int count);
PsnowSlot psnowGameSlot(int index,int count);
void psnowGenreInit(PsnowGenreModel *m,int count,int selected,int fps);
void psnowGameInit(PsnowGenreModel *m,int count,int selected,int fps);
void psnowGenreMove(PsnowGenreModel *m,int direction);
void psnowGenreStep(PsnowGenreModel *m,unsigned elapsedMs);
PsnowVec psnowGenrePosition(const PsnowGenreModel *m,int index);
/* Full category-cube ring; game-image slots retain the recovered layout. */
PsnowVec psnowGenreCubePosition(const PsnowGenreModel *m,int index);
float psnowGenreCubeAlpha(const PsnowGenreModel *m);
float psnowGenreLabelAlpha(const PsnowGenreModel *m,int index);
PsnowVec psnowGenreTransform(const PsnowGenreModel *m,PsnowVec local,int decoration);
float psnowGenreProjectionScale(const PsnowGenreModel *m);
float psnowDescriptionAlpha(float age);
float psnowDescriptionRight(float age);
float psnowSmallWaveScale(float age);
float psnowSmallWaveAge(const PsnowGenreModel *model,int index);
float psnowCrystalRefraction(float entryAge);
float psnowGenreExponent(float progress,float age,float duration,int fps);

#endif
