#include "ui/worker_lifecycle.h"
#include <kernel.h>
#include <unistd.h>

void deleteFinishedWorker(int threadId) {
  ee_thread_status_t status;
  // SignalSema may wake a higher-priority caller before the worker reaches
  // ExitThread. Yield so it can finish before deleting it or reusing its stack.
  while (ReferThreadStatus(threadId, &status) >= 0) {
    if (status.status == THS_DORMANT) {
      DeleteThread(threadId);
      return;
    }
    usleep(1000);
  }
}
