#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

extern bool thread_priority_cmp (const struct list_elem *a,
                                 const struct list_elem *b,
                                 void *aux);
void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);
  sema->value = value;
  list_init (&sema->waiters);
}
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  ASSERT (sema != NULL);
  ASSERT (!intr_context ());
  old_level = intr_disable ();
  while (sema->value == 0) 
    {

      list_insert_ordered (&sema->waiters,
                           &thread_current ()->elem,
                           thread_priority_cmp, NULL);
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}
bool
sema_try_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  bool success;
  ASSERT (sema != NULL);
  old_level = intr_disable ();
  if (sema->value > 0) 
    {
      sema->value--;
      success = true; 
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}
void
sema_up (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);

  old_level = intr_disable ();

  if (!list_empty (&sema->waiters)) 
    {
      list_sort(&sema->waiters, thread_priority_cmp, NULL);
      struct thread *t = list_entry(list_pop_front(&sema->waiters),
                                    struct thread, elem);
      thread_unblock(t);
    }
  sema->value++;
  intr_set_level (old_level);
  if (!intr_context())
    thread_yield();
  else
    intr_yield_on_return();
}
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);
  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
}
bool
lock_try_acquire (struct lock *lock)
{
  bool success;
  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));
  success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
  return success;
}
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));
  struct thread *cur = thread_current ();
  if (lock->holder != NULL)
    {
      cur->waiting_lock = lock;
      struct thread *t = lock->holder;
      while (t != NULL && cur->priority > t->priority)
        {
          t->priority = cur->priority;
          if (t == lock->holder)
            list_push_back (&t->donations, &cur->donation_elem);
          if (t->waiting_lock == NULL)
            break;
          t = t->waiting_lock->holder;
        }
    }
  sema_down (&lock->semaphore);
  cur->waiting_lock = NULL;
  lock->holder = cur;
}
void
lock_release (struct lock *lock) 
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  struct thread *cur = thread_current (); 
  struct list_elem *e = list_begin(&cur->donations);
  while (e != list_end(&cur->donations))
    {
      struct list_elem *next = list_next(e);
      struct thread *t = list_entry(e, struct thread, donation_elem);

      if (t->waiting_lock == lock)
        list_remove(e);

      e = next;
    }
  cur->priority = cur->original_priority;
  if (!list_empty(&cur->donations))
    {
      struct list_elem *e;
      for (e = list_begin(&cur->donations);
           e != list_end(&cur->donations);
           e = list_next(e))
        {
          struct thread *t = list_entry(e, struct thread, donation_elem);
          if (t->priority > cur->priority)
            cur->priority = t->priority;
        }
    }
  lock->holder = NULL;
  sema_up (&lock->semaphore);
}

bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);
  return lock->holder == thread_current ();
}
struct semaphore_elem 
{
  struct list_elem elem;
  struct semaphore semaphore;
};
bool
sema_elem_cmp (const struct list_elem *a,
               const struct list_elem *b,
               void *aux UNUSED)
{
  struct semaphore_elem *sa = list_entry(a, struct semaphore_elem, elem);
  struct semaphore_elem *sb = list_entry(b, struct semaphore_elem, elem);

  struct thread *ta = list_entry(list_front(&sa->semaphore.waiters),
                                struct thread, elem);
  struct thread *tb = list_entry(list_front(&sb->semaphore.waiters),
                                struct thread, elem);

  return ta->priority > tb->priority;
}
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);
  list_init (&cond->waiters);
}
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  sema_init (&waiter.semaphore, 0);
  list_insert_ordered(&cond->waiters,
                      &waiter.elem,
                      sema_elem_cmp, NULL);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);

  if (!list_empty (&cond->waiters)) 
    {
      list_sort(&cond->waiters, sema_elem_cmp, NULL);
      sema_up (&list_entry(list_pop_front(&cond->waiters),
                           struct semaphore_elem, elem)->semaphore);
    }
}
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}
