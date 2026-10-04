#ifndef LUNA_WORKER_TEST_KERNEL_H
#define LUNA_WORKER_TEST_KERNEL_H
#define THS_READY 2
#define THS_DORMANT 16
typedef struct { int status; } ee_thread_status_t;
int ReferThreadStatus(int threadId, ee_thread_status_t *status);
int DeleteThread(int threadId);
#endif
