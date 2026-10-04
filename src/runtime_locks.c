// Original LUNA code: Danny Nunez (dnunezx) 2026
#include <kernel.h>
#include <stdint.h>
#include <sys/lock.h>

// PS2SDK libcglue's lock ABI. Keep its initialization/release functions;
// override acquisition so older SDK archives cannot publish a phantom owner
// before taking the semaphore. malloc and stdio share these entry points.
struct __lock {
  int32_t sem_id;
  int32_t thread_id;
  int32_t count;
};

void __retarget_lock_acquire_recursive(_LOCK_T lock) {
  const int32_t thread_id = GetThreadId();
  if (lock->count > 0 && lock->thread_id == thread_id) {
    lock->count++;
    return;
  }
  WaitSema(lock->sem_id);
  // Ownership and recursion depth belong to the thread that acquired the gate.
  lock->thread_id = thread_id;
  lock->count = 1;
}

int __retarget_lock_try_acquire_recursive(_LOCK_T lock) {
  const int32_t thread_id = GetThreadId();
  if (lock->count > 0 && lock->thread_id == thread_id) {
    lock->count++;
    return 0;
  }
  if (PollSema(lock->sem_id) <= 0)
    return 1;
  lock->thread_id = thread_id;
  lock->count = 1;
  return 0;
}
