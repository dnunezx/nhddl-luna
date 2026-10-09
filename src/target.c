#include "target.h"
#include "common.h"
#include "devices/devices.h"
#include <errno.h>
#include <fcntl.h>
#include <ps2sdkapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Completely frees TargetList. Passed pointer will not be valid after this function executes
void freeTargetList(TargetList *result) {
  if (!result) return;
  free(result->byIndex);
  result->byIndex = NULL;
  if (result->borrowed) { free(result); return; }
  Target *target = result->first;
  while (target != NULL) {
    target = freeTarget(result, target);
  }
  result->first = NULL;
  result->last = NULL;
  result->total = 0;
  free(result);
}

// Finds target with given index in the list and returns a pointer to it
Target *getTargetByIdx(TargetList *targets, int idx) {
  if (!targets || idx < 0 || idx >= targets->total) return NULL;
  if (targets->byIndex) return targets->byIndex[idx];
  Target *current = targets->first;
  while (current != NULL) {
    if (current->idx == idx) {
      return current;
    }

    if (current->next == NULL)
      break;

    current = current->next;
  }
  return NULL;
}

// Makes and returns a deep copy of src without prev/next pointers.
Target *copyTarget(Target *src) {
  Target *copy = calloc(1, sizeof(Target));
  if (!copy) return NULL;
  copy->idx = src->idx;
  copy->platform = src->platform;

  copy->fullPath = strdup(src->fullPath);
  copy->name = strdup(src->name);
  copy->id = src->id ? strdup(src->id) : NULL;
  if (!copy->fullPath || !copy->name || (src->id && !copy->id)) {
    free(copy->fullPath);
    free(copy->name);
    free(copy->id);
    free(copy);
    return NULL;
  }
  copy->device = src->device;

  return copy;
}

TargetList *createTargetView(const TargetList *source) {
  if (!source || source->total < 0) return NULL;
  TargetList *view = calloc(1, sizeof(*view));
  if (!view) return NULL;
  view->borrowed = 1;
  view->indexCapacity = source->total;
  if (source->total) {
    view->byIndex = malloc((size_t)source->total * sizeof(*view->byIndex));
    if (!view->byIndex) { freeTargetList(view); return NULL; }
  }
  return view;
}

int filterTargetView(TargetList *view, const TargetList *source,
                     TargetFilter platform, const uint8_t *favorites) {
  if (!view || !source || view == source || !view->borrowed ||
      source->borrowed || source->total > view->indexCapacity ||
      platform < TARGET_MIXED || platform > TARGET_PS1_ONLY) return -EINVAL;
  view->total = 0;
  for (const Target *t = source->first; t; t = t->next) {
    if ((platform == TARGET_PS2_ONLY && t->platform != TARGET_PS2) ||
        (platform == TARGET_PS1_ONLY && t->platform != TARGET_PS1) ||
        (favorites && !favorites[t->idx])) continue;
    view->byIndex[view->total++] = (Target *)t;
  }
  view->first = view->total ? view->byIndex[0] : NULL;
  view->last = view->total ? view->byIndex[view->total - 1] : NULL;
  return 0;
}

int targetViewIndex(const TargetList *view, const Target *target) {
  if (!view || !target) return -1;
  for (int i = 0; i < view->total; ++i)
    if (getTargetByIdx((TargetList *)view, i) == target) return i;
  return -1;
}

const char *targetFilterLabel(TargetFilter filter) {
  return filter == TARGET_PS2_ONLY ? "PS2" : filter == TARGET_PS1_ONLY ? "PS1" : "Mix";
}

// Compare unsigned ASCII bytes without allocating uppercase name copies.
static int compareTargetNames(const void *left, const void *right) {
  const Target *a = *(Target *const *)left;
  const Target *b = *(Target *const *)right;
  const unsigned char *nameA = (const unsigned char *)a->name;
  const unsigned char *nameB = (const unsigned char *)b->name;
  while (1) {
    unsigned char ca = *nameA++, cb = *nameB++;
    if (ca >= 'a' && ca <= 'z') ca -= 'a' - 'A';
    if (cb >= 'a' && cb <= 'z') cb -= 'a' - 'A';
    if (ca != cb) return (int)ca - (int)cb;
    if (!ca) return (int)a->idx - (int)b->idx;
  }
}

static void discardTargetIndex(TargetList *result) {
  free(result->byIndex);
  result->byIndex = NULL;
  result->indexCapacity = 0;
}

void appendTarget(TargetList *result, Target *title) {
  discardTargetIndex(result);
  title->prev = result->last;
  title->next = NULL;
  if (result->last) result->last->next = title;
  else result->first = title;
  result->last = title;
  result->total++;
}

int buildTargetIndex(TargetList *result) {
  if (!result || result->borrowed || result->total < 0) return -EINVAL;
  // Target.idx is 16-bit; reject overflow instead of publishing duplicate IDs.
  if ((unsigned)result->total > (unsigned)UINT16_MAX + 1U) return -EOVERFLOW;
  Target **index = result->total ? malloc((size_t)result->total * sizeof(*index)) : NULL;
  if (result->total && !index) return -ENOMEM;
  int count = 0;
  for (Target *target = result->first; target; target = target->next) {
    if (count >= result->total) { free(index); return -EINVAL; }
    index[count++] = target;
  }
  if (count != result->total) { free(index); return -EINVAL; }
  discardTargetIndex(result);
  result->byIndex = index;
  result->indexCapacity = count;
  for (int i = 0; i < count; i++) index[i]->idx = (uint16_t)i;
  return 0;
}

int sortTargetList(TargetList *result) {
  int error = buildTargetIndex(result);
  if (error) return error;
  if (result->total > 1)
    qsort(result->byIndex, (size_t)result->total, sizeof(*result->byIndex),
          compareTargetNames);
  for (int i = 0; i < result->total; i++) {
    Target *target = result->byIndex[i];
    target->idx = (uint16_t)i;
    target->prev = i ? result->byIndex[i - 1] : NULL;
    target->next = i + 1 < result->total ? result->byIndex[i + 1] : NULL;
  }
  result->first = result->total ? result->byIndex[0] : NULL;
  result->last = result->total ? result->byIndex[result->total - 1] : NULL;
  return 0;
}

// Completely frees Target and returns pointer to the next target in the list
Target *freeTarget(TargetList *targetList, Target *target) {
  discardTargetIndex(targetList);
  targetList->total--;
  // Update target list if target is the first or the last element
  if (targetList->first == target) {
    targetList->first = target->next;
  }
  if (targetList->last == target) {
    targetList->last = target->prev;
  }

  Target *next = NULL;
  // If target has a link to the next element
  if (target->next != NULL) {
    // Set return pointer
    next = target->next;
    if (target->prev != NULL) {
      // If target has a link to the previous element, link prev and next together
      next->prev = target->prev;
      target->prev->next = next;
    } else {
      // Else, remove link to target
      next->prev = NULL;
    }
  } else if (target->prev != NULL) {
    // If target doesn't have a link to the next element
    // but has a link to the previous element, remove link to target
    target->prev->next = NULL;
  }

  free(target->fullPath);
  free(target->name);
  if (target->id != NULL)
    free(target->id);

  free(target);
  return next;
}
