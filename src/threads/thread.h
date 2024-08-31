#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_SLEEPING,    /* Thread is on the sleep queue; This helps us distinguish between the case where 
                           the thread is actually waiting for a resource*/
    THREAD_DYING        /* About to be destroyed. */
  };

/* track's if a thread is a priority donee. */
enum priority_donee_status
{
    PRIORITY_DONEE=51,
    PRIORITY_NON_DONEE=52
};


/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int32_t tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    uint8_t priority;                   /* Priority. */
//    enum priority_donee_status donee_status;                   /* Priority. */
    uint8_t donee_priority;             /* store the donee's original priority here; this helps with cases where the donee receives multiple priority donations */
    uint8_t original_priority;
    struct thread* donee_thread;        /* this helps us with nested priority donations */ 
    struct list_elem allelem;           /* List element for all threads list. */
    uint32_t number_of_donors;

    /* Shared between thread.c and synch.c &timer.c . */
    struct list_elem elem;              /* List element. */
    //struct list_elem priority_donors_list_elem;
    //struct list priority_donors_list ;
#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
#endif
   int64_t sleep_time;
   int64_t elapsed_sleep_time;
    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

#define SCHED_PRIORITY_PREMPTIVE 1
#define SCHED_POLICY SCHED_PRIORITY_PREMPTIVE

extern struct list ready_list;
extern struct list all_list;
void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef struct list_elem* thread_action_func_return_next (struct thread *t, void *aux);
typedef void thread_action_func(struct thread *t, void *aux);

void thread_foreach (thread_action_func *, void *);
void thread_foreach_inlist(struct list* thread_list, thread_action_func_return_next *func, void *aux);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void thread_place_on_list_per_sched_policy(struct list* resource_list, struct list_elem* thread);
bool is_thread_from_list_elemA_high_priority(const struct list_elem* list_elemA, const struct list_elem* list_elemB, void* aux);

#endif /* threads/thread.h */
