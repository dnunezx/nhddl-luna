#ifndef LUNA_PSNOW_GENRE_RENDER_H
#define LUNA_PSNOW_GENRE_RENDER_H
#include "ui/psnow_genre_model.h"
#include <gsKit.h>
int psnowRingOpen(void);
void psnowRingClose(void);
void psnowRingDraw(const PsnowGenreModel *model);
void psnowDescriptionDraw(const PsnowGenreModel *model,float cx,float cy,float halfWidth,
                           float age);
int psnowGameCoverDraw(GSTEXTURE *texture,float left,float top,float right,float bottom,
                        float shade,float alpha);
#endif
