#ifndef LUNA_UI_UTF8_H
#define LUNA_UI_UTF8_H
#include <stdint.h>

// Decode one scalar, advancing at least one byte on malformed input. Never
// inspect bytes beyond the terminator; ASCII paths and labels stay compatible.
static inline uint32_t lunaUTF8Next(const char **cursor) {
  const unsigned char *p = (const unsigned char *)*cursor;
  uint32_t value = *p;
  if (!value) return 0;
  int length;
  uint32_t minimum;
  if (value < 0x80) { *cursor += 1; return value; }
  if (value >= 0xC2 && value <= 0xDF) { length = 2; minimum = 0x80; value &= 0x1F; }
  else if (value >= 0xE0 && value <= 0xEF) { length = 3; minimum = 0x800; value &= 0x0F; }
  else if (value >= 0xF0 && value <= 0xF4) { length = 4; minimum = 0x10000; value &= 7; }
  else { *cursor += 1; return '?'; }
  for (int i = 1; i < length; i++) {
    if (!p[i] || (p[i] & 0xC0) != 0x80) { *cursor += 1; return '?'; }
    value = (value << 6) | (p[i] & 0x3F);
  }
  if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
    *cursor += 1; return '?';
  }
  *cursor += length;
  return value;
}
#endif
