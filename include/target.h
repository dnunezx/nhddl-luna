#ifndef _TARGET_H_
#define _TARGET_H_

#include "common.h"
#include <stdint.h>

// Defined in devices.h
struct DeviceMapEntry;

// An entry in TargetList
typedef struct Target {
  uint16_t idx;           // ISO index (monotonically increasing). Used to uniquely identify the list entry
  char *fullPath;         // Full path to ISO
  char *name;             // Target name (extracted from file name)
  char *id;               // Title ID
  struct DeviceMapEntry *device; // Device entry

  struct Target *prev; // Previous target in the list
  struct Target *next; // Next target in the list
} Target;

// A linked list of launch candidates
typedef struct {
  int total;     // Total number of targets
  Target *first; // First target
  Target *last;  // Last target
  Target **byIndex; // Owned pointer array; entries remain owned by the linked list.
  int indexCapacity;
  int borrowed; // Filtered views borrow targets and must never free or relink them.
} TargetList;

// Completely frees TargetList. Passed pointer will not be valid after this function executes
void freeTargetList(TargetList *result);

// Finds target with given index in the list and returns a pointer to it
Target *getTargetByIdx(TargetList *targets, int idx);

// Makes and returns a deep copy of src without prev/next pointers.
Target *copyTarget(Target *src);

// Collect titles without sorting; mutation discards any existing index.
void appendTarget(TargetList *result, Target *title);
// Build constant-time lookups in current order, assigning canonical indexes.
int buildTargetIndex(TargetList *result);
// Sort once, preserving discovery order for equal case-insensitive names.
int sortTargetList(TargetList *result);

// Removes and frees Target, decrements total and invalidates the index.
Target *freeTarget(TargetList *targetList, Target *target);

#endif
