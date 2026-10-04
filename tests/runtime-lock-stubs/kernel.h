#ifndef LUNA_TEST_RUNTIME_KERNEL_H
#define LUNA_TEST_RUNTIME_KERNEL_H
int GetThreadId(void);
int WaitSema(int id);
int PollSema(int id);
#endif
