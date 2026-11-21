#ifndef ROCKET_QUICK_PLAY_FORMAT_H
#define ROCKET_QUICK_PLAY_FORMAT_H
#include <stdint.h>
#include <stddef.h>
#define ROCKET_QUICK_PLAY_MAGIC 0x4c505152u /* RQPL */
#define ROCKET_QUICK_PLAY_WIDTH 140
#define ROCKET_QUICK_PLAY_HEIGHT 200
#define ROCKET_QUICK_PLAY_PIXELS (140 * 200 * 4)
#define ROCKET_QUICK_PLAY_GAME 1u
#define ROCKET_QUICK_PLAY_COVER 2u
#define ROCKET_QUICK_PLAY_SAMPLE_RECORD 4u
typedef struct {
  uint32_t magic;
  uint16_t version,headerBytes;
  uint32_t flags,timestamp,payloadBytes,coverHash;
  uint16_t width,height;
  uint32_t headerHash;
  char titleId[16],title[96],relativePath[256];
  uint8_t reserved[112];
} RocketQuickPlay;
typedef char RocketQuickPlaySizeCheck[sizeof(RocketQuickPlay)==512?1:-1];
static inline uint32_t rocketQuickPlayHash(const void *data,size_t size) {
  const uint8_t *p=data;uint32_t hash=2166136261u;
  while(size--)hash=(hash^*p++)*16777619u;
  return hash;
}
static inline uint32_t rocketQuickPlayHeaderHash(const RocketQuickPlay *header) {
  RocketQuickPlay copy=*header;copy.headerHash=0;
  return rocketQuickPlayHash(&copy,sizeof(copy));
}
static inline int rocketQuickPlayValid(const RocketQuickPlay *h) {
  if(h->magic!=ROCKET_QUICK_PLAY_MAGIC||h->version!=1||h->headerBytes!=sizeof(*h)||
     !(h->flags&ROCKET_QUICK_PLAY_GAME)||(h->flags&~7u)||
     h->width!=140||h->height!=200||h->headerHash!=rocketQuickPlayHeaderHash(h))return 0;
  if(h->payloadBytes!=((h->flags&ROCKET_QUICK_PLAY_COVER)?ROCKET_QUICK_PLAY_PIXELS:0))return 0;
  if(!h->titleId[0]||!h->title[0]||!h->relativePath[0]||
     h->titleId[15]||h->title[95]||h->relativePath[255])return 0;
  return 1;
}
#endif
