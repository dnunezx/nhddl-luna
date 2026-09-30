#ifndef STORAGE_TEST_PS2SDK_H
#define STORAGE_TEST_PS2SDK_H
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
static inline size_t lunaTestStrlcpy(char *to, const char *from, size_t size) {
  size_t length = strlen(from);
  if (size) {
    size_t copy = length < size ? length : size - 1;
    memcpy(to, from, copy);
    to[copy] = '\0';
  }
  return length;
}
#define strlcpy lunaTestStrlcpy
#endif
