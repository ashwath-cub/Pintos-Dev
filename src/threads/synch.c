/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#define NESTED_PRIORITY_DONATION_LIMIT 8

static void handle_priority_donation(struct lock *lock);
static void reset_priority_donation(struct lock *lock);

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0) 
  {
    list_insert_ordered(&sema->waiters, &thread_current ()->elem, is_thread_from_list_elemA_high_priority, NULL);
    thread_block ();
  }
  sema->value--;
  intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
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

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  sema->value++;
  if (!list_empty (&sema->waiters)) 
    thread_unblock (list_entry (list_pop_front (&sema->waiters),
                                struct thread, elem));
  intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) 
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++) 
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) 
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++) 
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
}


static void handle_priority_donation(struct lock *lock)
{
  enum intr_level old_level;
  struct thread* lock_holder_thread_ptr, *current_thread_ptr;
  uint8_t nested_priority_donation=0;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  lock_holder_thread_ptr = lock->holder ;
  current_thread_ptr=thread_current();
  // this indicates lock has been acquired by some other thread
  // and is a case where donation is needed
  if( (lock_holder_thread_ptr!=NULL) && (lock_holder_thread_ptr->priority < current_thread_ptr->priority) && lock->semaphore.value==0 ) 
  {
    // set donee thread value within current thread to lock holder 
    current_thread_ptr->donee_thread = lock_holder_thread_ptr ;
    // add current thread to donor's list of lock holder
    list_insert_ordered(&lock_holder_thread_ptr->priority_donors, &current_thread_ptr->donor_elem, is_thread_from_list_elemA_high_priority, NULL);
  do 
  { 
    // priority donation is required here
    lock_holder_thread_ptr->priority=current_thread_ptr->priority;
    
    // if the donee's status is blocked/ready(it could also be sleeping; in which case no action is needed), 
    // re-add to the queue in which it is placed
    if(lock_holder_thread_ptr->status!=THREAD_SLEEPING)
    {
      list_reinsert_ordered(&lock_holder_thread_ptr->elem, is_thread_from_list_elemA_high_priority); 
    }

    if(lock_holder_thread_ptr->donee_thread!=NULL)
    {
      // re adjust position on it's donee's donors list
      list_reinsert_ordered(&lock_holder_thread_ptr->donor_elem, is_thread_from_list_elemA_high_priority);

    }

    lock_holder_thread_ptr = lock_holder_thread_ptr->donee_thread;
    nested_priority_donation ++;
  }
  //check nested donation requirement; dont donate if nested donation limit reached or if the lock holder's priority is higher 
  while ( (lock_holder_thread_ptr!=NULL)&&(nested_priority_donation<NESTED_PRIORITY_DONATION_LIMIT)&& ( lock_holder_thread_ptr->priority < current_thread_ptr->priority ) );
  
  }
  
  while (lock->semaphore.value==0) 
  {
    list_insert_ordered(&lock->semaphore.waiters, &thread_current ()->elem, is_thread_from_list_elemA_high_priority, NULL);
    thread_block();
  }
  lock->holder=thread_current();
  lock->semaphore.value--;
  intr_set_level (old_level);  
}

static void reset_priority_donation(struct lock *lock)
{
  struct thread* current_thread = thread_current();
  enum intr_level old_level;
  struct thread *t;
  struct list_elem* e;
  old_level = intr_disable ();
  // check to see if current thread received a priority donation
  if(current_thread->original_priority != current_thread->priority)
  {
    // scan all threads waiting on the lock to see who donated
    // in case of multiple donations associated with different locks, handle for the lock in consideration here
    // when all the locks are released, the priority will be reset aptly 
    // (NOTE: key here is that it shouldnt be reset to it's original priority carelessly)
    // On Nested Donations: 
    // Nested donation can be simplified by thinking about it as if the original donor's priority increased
    // Given that the way we reset our priority has nothing to do with the current lock(and donor) we are safe
    
    for (e = list_begin (&lock->semaphore.waiters); e != list_end (&lock->semaphore.waiters);
       e = list_next (e))
    {
      t = list_entry (e, struct thread, elem);
      if(t->donee_thread==current_thread)
      {
        //match found; reset value in donor thread; else we may end up with an associated nested priority donation
        t->donee_thread = NULL;
        // remove match from donor's list
        list_remove(&t->donor_elem);
      }
    }

    // Now that all threads who have donated associated with the current lock have been removed from the list we are okay to set our new 
    // priority to the new max within the donor's list which will be at the front 
    // if donor's list is empty, we are able to reset our priority to original priority  
    if ( !list_empty( &current_thread->priority_donors ) )
    {
      e = list_front( &current_thread->priority_donors );
      t = list_entry( e, struct thread, donor_elem);
      current_thread->priority = t->priority;
    }
    else
    {
      current_thread->priority = current_thread->original_priority;
    }

  }
  
  lock->semaphore.value++;
  lock->holder=NULL;
  if (!list_empty (&lock->semaphore.waiters)) 
    thread_unblock (list_entry (list_pop_front (&lock->semaphore.waiters),
                                struct thread, elem));
  intr_set_level(old_level);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  if( thread_mlfqs == true )
  {
    sema_down(&lock->semaphore);
    lock->holder=thread_current();
  }
  else
  {
    handle_priority_donation(lock);
  }
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
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

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) 
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));
  
  if(thread_mlfqs==false)
  {
    reset_priority_donation(lock);
  }
  else
  {
    sema_up (&lock->semaphore);
    lock->holder=NULL;
  }
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem 
  {
    struct list_elem elem;              /* List element. */
    struct semaphore semaphore;         /* This semaphore. */
    uint8_t priority;
  };

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

static bool is_semaphore_from_list_elemA_high_priority(const struct list_elem* list_elemA, const struct list_elem* list_elemB, void* aux UNUSED)
{
  struct semaphore_elem * semA= list_entry(list_elemA, struct semaphore_elem, elem);
  struct semaphore_elem * semB= list_entry(list_elemB, struct semaphore_elem, elem);
  uint8_t priority_semA=semA->priority;
  uint8_t priority_semB=semB->priority;
  bool return_val=false;

  if(priority_semA>priority_semB)
  {
    return_val=true;
  }

  return return_val;
}


/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  sema_init (&waiter.semaphore, 0);

  
  if(thread_mlfqs == true)
  {
    list_push_back (&cond->waiters, &waiter.elem);
  }  
  else
  {
    //also tie current thread's priority to this semaphore
    waiter.priority = thread_current()->priority;

    list_insert_ordered(&cond->waiters, &waiter.elem, is_semaphore_from_list_elemA_high_priority, NULL);
  }
    

  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters)) 
    sema_up (&list_entry (list_pop_front (&cond->waiters),
                          struct semaphore_elem, elem)->semaphore);
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}
