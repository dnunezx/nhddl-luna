/*
#
# Graphics Synthesizer Mode Selector (a.k.a. GSM) - Force (set and keep) a GS Mode, then load & exec a PS2 ELF
#-------------------------------------------------------------------------------------------------------------
# Copyright 2009, 2010, 2011 doctorxyz & dlanor
# Copyright 2011, 2012 doctorxyz, SP193 & reprep
# Copyright 2013 Bat Rastard
# Copyright 2014, 2015, 2016 doctorxyz
# Licenced under Academic Free License version 2.0
# Review LICENSE file for further details.
#
*/

// Presets adapted from the pinned OPL src/gsm.c. Preserve its order.
#ifndef LUNA_OPL_GSM_PRESETS_H
#define LUNA_OPL_GSM_PRESETS_H

#include <gsKit.h>
#include "ui/game_options.h"

#define LUNA_GSM_DISPLAY(DH, DW, MAGV, MAGH, DY, DX) \
  (((u64)(DH) << 44) | ((u64)(DW) << 32) | ((u64)(MAGV) << 27) | \
   ((u64)(MAGH) << 23) | ((u64)(DY) << 12) | (u64)(DX))
#define LUNA_GSM_SYNCV(VS, VDP, VBPE, VBP, VFPE, VFP) \
  (((u64)(VS) << 53) | ((u64)(VDP) << 42) | ((u64)(VBPE) << 32) | \
   ((u64)(VBP) << 20) | ((u64)(VFPE) << 10) | (u64)(VFP))

typedef struct {
  s16 interlace, mode, ffmd;
  u64 display, syncv;
} LunaOplGsmPreset;

static const LunaOplGsmPreset lunaOplGsmPresets[LUNA_OPL_VIDEO_MODE_COUNT - 1] = {
        //                                                            DH    DW   MAGV MAGH  DY   DX              VS  VDP  VBPE  VBP VFPE  VFP
        {GS_INTERLACED,    GS_MODE_NTSC,        GS_FIELD, LUNA_GSM_DISPLAY(447,  2559, 0,   3,   46,  700), LUNA_GSM_SYNCV(6,  480,  6,    26,  6,   1)},
        {GS_INTERLACED,    GS_MODE_NTSC,        GS_FRAME, LUNA_GSM_DISPLAY(223,  2559, 0,   3,   26,  700), LUNA_GSM_SYNCV(6,  480,  6,    26,  6,   2)},
        {GS_INTERLACED,    GS_MODE_PAL,         GS_FIELD, LUNA_GSM_DISPLAY(511,  2559, 0,   3,   70,  720), LUNA_GSM_SYNCV(5,  576,  5,    33,  5,   1)},
        {GS_INTERLACED,    GS_MODE_PAL,         GS_FRAME, LUNA_GSM_DISPLAY(255,  2559, 0,   3,   37,  720), LUNA_GSM_SYNCV(5,  576,  5,    33,  5,   4)},
        {GS_INTERLACED,    GS_MODE_PAL,         GS_FIELD, LUNA_GSM_DISPLAY(447,  2559, 0,   3,   46,  700), LUNA_GSM_SYNCV(6,  480,  6,    26,  6,   1)},
        {GS_INTERLACED,    GS_MODE_PAL,         GS_FRAME, LUNA_GSM_DISPLAY(223,  2559, 0,   3,   26,  700), LUNA_GSM_SYNCV(6,  480,  6,    26,  6,   2)},
        {GS_NONINTERLACED, GS_MODE_DTV_480P,    GS_FRAME, LUNA_GSM_DISPLAY(255,  2559, 0,   1,   12,  736), LUNA_GSM_SYNCV(6,  483,  3072, 30,  0,   6)},
        {GS_NONINTERLACED, GS_MODE_DTV_576P,    GS_FRAME, LUNA_GSM_DISPLAY(255,  2559, 0,   1,   23,  756), LUNA_GSM_SYNCV(5,  576,  0,    39,  0,   5)},
        {GS_NONINTERLACED, GS_MODE_DTV_480P,    GS_FRAME, LUNA_GSM_DISPLAY(479,  1439, 0,   1,   35,  232), LUNA_GSM_SYNCV(6,  483,  3072, 30,  0,   6)},
        {GS_NONINTERLACED, GS_MODE_DTV_576P,    GS_FRAME, LUNA_GSM_DISPLAY(575,  1439, 0,   1,   44,  255), LUNA_GSM_SYNCV(5,  576,  0,    39,  0,   5)},
        {GS_NONINTERLACED, GS_MODE_DTV_720P,    GS_FRAME, LUNA_GSM_DISPLAY(719,  1279, 1,   1,   24,  302), LUNA_GSM_SYNCV(5,  720,  0,    20,  0,   5)},
        {GS_INTERLACED,    GS_MODE_DTV_1080I,   GS_FIELD, LUNA_GSM_DISPLAY(1079, 1919, 1,   2,   48,  238), LUNA_GSM_SYNCV(10, 1080, 2,    28,  0,   5)},
        {GS_INTERLACED,    GS_MODE_DTV_1080I,   GS_FRAME, LUNA_GSM_DISPLAY(1079, 1919, 0,   2,   48,  238), LUNA_GSM_SYNCV(10, 1080, 2,    28,  0,   5)},
        {GS_NONINTERLACED, GS_MODE_VGA_640_60,  GS_FRAME, LUNA_GSM_DISPLAY(479,  1279, 0,   1,   54,  276), LUNA_GSM_SYNCV(2,  480,  0,    33,  0,   10)},
        {GS_NONINTERLACED, GS_MODE_VGA_640_72,  GS_FRAME, LUNA_GSM_DISPLAY(479,  1279, 0,   1,   18,  330), LUNA_GSM_SYNCV(3,  480,  0,    28,  0,   9)},
        {GS_NONINTERLACED, GS_MODE_VGA_640_75,  GS_FRAME, LUNA_GSM_DISPLAY(479,  1279, 0,   1,   18,  360), LUNA_GSM_SYNCV(3,  480,  0,    16,  0,   1)},
        {GS_NONINTERLACED, GS_MODE_VGA_640_85,  GS_FRAME, LUNA_GSM_DISPLAY(479,  1279, 0,   1,   18,  260), LUNA_GSM_SYNCV(3,  480,  0,    16,  0,   1)},
        {GS_INTERLACED,    GS_MODE_VGA_640_60,  GS_FIELD, LUNA_GSM_DISPLAY(959,  1279, 1,   1,   128, 291), LUNA_GSM_SYNCV(2,  992,  0,    33,  0,   10)},
        {GS_NONINTERLACED, GS_MODE_VGA_800_56,  GS_FRAME, LUNA_GSM_DISPLAY(599,  1599, 0,   1,   25,  450), LUNA_GSM_SYNCV(2,  600,  0,    22,  0,   1)},
        {GS_NONINTERLACED, GS_MODE_VGA_800_60,  GS_FRAME, LUNA_GSM_DISPLAY(599,  1599, 0,   1,   25,  465), LUNA_GSM_SYNCV(4,  600,  0,    23,  0,   1)},
        {GS_NONINTERLACED, GS_MODE_VGA_800_72,  GS_FRAME, LUNA_GSM_DISPLAY(599,  1599, 0,   1,   25,  465), LUNA_GSM_SYNCV(6,  600,  0,    23,  0,   37)},
        {GS_NONINTERLACED, GS_MODE_VGA_800_75,  GS_FRAME, LUNA_GSM_DISPLAY(599,  1599, 0,   1,   25,  510), LUNA_GSM_SYNCV(3,  600,  0,    21,  0,   1)},
        {GS_NONINTERLACED, GS_MODE_VGA_800_85,  GS_FRAME, LUNA_GSM_DISPLAY(599,  1599, 0,   1,   15,  500), LUNA_GSM_SYNCV(3,  600,  0,    27,  0,   1)},
        {GS_NONINTERLACED, GS_MODE_VGA_1024_60, GS_FRAME, LUNA_GSM_DISPLAY(767,  2047, 0,   2,   30,  580), LUNA_GSM_SYNCV(6,  768,  0,    29,  0,   3)},
        {GS_NONINTERLACED, GS_MODE_VGA_1024_70, GS_FRAME, LUNA_GSM_DISPLAY(767,  1023, 0,   0,   30,  266), LUNA_GSM_SYNCV(6,  768,  0,    29,  0,   3)},
        {GS_NONINTERLACED, GS_MODE_VGA_1024_75, GS_FRAME, LUNA_GSM_DISPLAY(767,  1023, 0,   0,   30,  260), LUNA_GSM_SYNCV(3,  768,  0,    28,  0,   1)},
        {GS_NONINTERLACED, GS_MODE_VGA_1024_85, GS_FRAME, LUNA_GSM_DISPLAY(767,  1023, 0,   0,   30,  290), LUNA_GSM_SYNCV(3,  768,  0,    36,  0,   1)},
        {GS_NONINTERLACED, GS_MODE_VGA_1280_60, GS_FRAME, LUNA_GSM_DISPLAY(1023, 1279, 1,   1,   40,  350), LUNA_GSM_SYNCV(3,  1024, 0,    38,  0,   1)},
        {GS_NONINTERLACED, GS_MODE_VGA_1280_75, GS_FRAME, LUNA_GSM_DISPLAY(1023, 1279, 1,   1,   40,  350), LUNA_GSM_SYNCV(3,  1024, 0,    38,  0,   1)}};

#endif
