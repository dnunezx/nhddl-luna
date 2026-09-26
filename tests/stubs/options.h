#ifndef LUNA_TEST_OPTIONS_H
#define LUNA_TEST_OPTIONS_H

typedef struct Argument {
  char *arg;
  char *value;
  int isDisabled;
  int isGlobal;
  struct Argument *prev;
  struct Argument *next;
} Argument;

typedef struct {
  int total;
  Argument *first;
  Argument *last;
} ArgumentList;

Argument *getArgument(ArgumentList *list, const char *name);
Argument *insertArgument(ArgumentList *list, const char *name, char *value);

#endif
