#ifndef LUNA_WORKER_LIFECYCLE_H
#define LUNA_WORKER_LIFECYCLE_H

// Call after the worker's completion semaphore is received. The worker exits
// with ExitThread; its static stack cannot be reused until it is dormant.
void deleteFinishedWorker(int threadId);

#endif
