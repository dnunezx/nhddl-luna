#ifndef LUNA_TEST_RUNTIME_LOCK_H
#define LUNA_TEST_RUNTIME_LOCK_H
struct __lock;
typedef struct __lock *_LOCK_T;
void __retarget_lock_acquire_recursive(_LOCK_T lock);
int __retarget_lock_try_acquire_recursive(_LOCK_T lock);
#endif
