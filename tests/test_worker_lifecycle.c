// Simulate a caller preempting its worker immediately after the done signal.
#include "ui/worker_lifecycle.h"
#include <kernel.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>

static int alive, dormant, yields, deletes;
int ReferThreadStatus(int threadId, ee_thread_status_t *status) {
  assert(threadId == 7);
  if (!alive) return -1;
  status->status = dormant ? THS_DORMANT : THS_READY;
  return threadId;
}
int DeleteThread(int threadId) {
  assert(threadId == 7);
  if (!dormant) return -1; // EE cannot delete a running/ready thread.
  deletes++;
  alive = 0;
  return threadId;
}
int usleep(useconds_t duration) {
  assert(duration == 1000);
  yields++;
  // The worker still has instructions to execute after signalling done.
  if (yields == 2) dormant = 1;
  return 0;
}
int main(void) {
  alive = 1;
  assert(DeleteThread(7) < 0); // Reproduce the old unchecked deletion.
  assert(alive && !dormant);
  deleteFinishedWorker(7);
  assert(!alive && dormant && deletes == 1 && yields == 2);
  alive = 1;
  deleteFinishedWorker(7); // Already exited: no unnecessary delay.
  assert(!alive && deletes == 2 && yields == 2);
  deleteFinishedWorker(7); // Missing thread: do not delete a stale ID.
  assert(deletes == 2 && yields == 2);
  puts("Worker completion preemption and safe stack reuse tests passed");
  return 0;
}
