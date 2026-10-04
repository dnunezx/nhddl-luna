#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/lock.h>

struct __lock {
  int32_t sem_id, thread_id, count;
};

static struct __lock lock;
static int thread_id, gate, waits, polls;
static void (*before_take)(void);

int GetThreadId(void) { return thread_id; }

// Model the SDK's unchanged recursive release operation.
static void release(void) {
  assert(lock.thread_id == thread_id && lock.count > 0);
  if (--lock.count == 0) {
    lock.thread_id = -1;
    assert(gate == 0);
    gate = 1;
  }
}

static void run_preemption(void) {
  if (before_take != NULL) {
    void (*hook)(void) = before_take;
    before_take = NULL;
    hook();
  }
}

int WaitSema(int id) {
  assert(id == lock.sem_id);
  waits++;
  run_preemption();
  // An old acquisition loses the gate here after the higher-priority thread
  // releases a phantom recursion. Fail immediately instead of hanging a test.
  assert(gate == 1);
  gate = 0;
  return id;
}

int PollSema(int id) {
  assert(id == lock.sem_id);
  polls++;
  run_preemption();
  if (!gate)
    return -1;
  gate = 0;
  return id;
}

static void reset(void) {
  lock = (struct __lock){7, -1, 0};
  thread_id = gate = 1;
  waits = polls = 0;
  before_take = NULL;
}

static void higher_priority_worker(void) {
  const int interrupted_thread = thread_id;
  thread_id = 2;
  __retarget_lock_acquire_recursive(&lock);
  assert(lock.thread_id == 2 && lock.count == 1);
  release();
  thread_id = interrupted_thread;
}

int main(void) {
  reset();
  before_take = higher_priority_worker;
  __retarget_lock_acquire_recursive(&lock);
  assert(lock.thread_id == 1 && lock.count == 1 && gate == 0);
  __retarget_lock_acquire_recursive(&lock);
  assert(lock.count == 2 && waits == 2);
  assert(__retarget_lock_try_acquire_recursive(&lock) == 0);
  assert(lock.count == 3 && polls == 0);
  release(); release(); release();
  assert(lock.count == 0 && gate == 1);

  reset();
  before_take = higher_priority_worker;
  assert(__retarget_lock_try_acquire_recursive(&lock) == 0);
  assert(lock.thread_id == 1 && lock.count == 1 && gate == 0);
  thread_id = 2;
  assert(__retarget_lock_try_acquire_recursive(&lock) == 1);
  assert(lock.thread_id == 1 && lock.count == 1 && gate == 0);
  thread_id = 1;
  release();
  thread_id = 2;
  __retarget_lock_acquire_recursive(&lock);
  assert(lock.thread_id == 2 && lock.count == 1);
  release();
  puts("Runtime lock preemption, recursion and failed-try tests passed");
  return 0;
}
