#ifndef STORAGE_TEST_LIBCDVD_H
#define STORAGE_TEST_LIBCDVD_H
enum { SCECdINoD, SCECdEXIT };
typedef struct { unsigned char stat, second, minute, hour, pad, day, month, year; } sceCdCLOCK;
int sceCdInit(int mode);
int sceCdReadClock(sceCdCLOCK *clock);
static inline unsigned btoi(unsigned value) { return (value >> 4) * 10 + (value & 15); }
#endif
