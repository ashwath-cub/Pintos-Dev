#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#include "fixed_point.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

#define NUMBER_OF_PRIORITIES_IN_MLFQS_SCHEDULER       64
#define NUMBER_OF_READY_QUEUES_IN_MLFQS_SCHEDULER     NUMBER_OF_PRIORITIES_IN_MLFQS_SCHEDULER

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
struct list ready_list={0};

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
struct list ready_list_mlfqs[64]={0};


/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
struct list all_list={0};

/*list of all threads who've donated their priority*/
struct list priority_donors_list={0};
/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */



static volatile uint32_t load_average=0;
volatile uint32_t ready_threads=0;
struct lock ready_threads_lock;
/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);
  uint8_t ready_list_init_index;
  lock_init (&tid_lock);
  list_init (&all_list);

  if( thread_mlfqs == true )
  {
    // initialize all 64 ready queues for mlfqs
    for( ready_list_init_index = 0; ready_list_init_index<NUMBER_OF_READY_QUEUES_IN_MLFQS_SCHEDULER ;ready_list_init_index++)
    {
      list_init( &ready_list_mlfqs[ ready_list_init_index ] );
    }
    lock_init(&ready_threads_lock);
  }
  else
  {
    list_init (&ready_list);
  }


  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

static struct list_elem* wakeup_sleeping_thread_if_its_time_return_next(struct thread *t, void *aux)
{
  t->elapsed_sleep_time ++ ;
  // save next_elem, before the current elem is placed on a different list
  struct list_elem* next_elem = list_next(&t->elem); 
  if(t->elapsed_sleep_time >= t->sleep_time)
  {
    /* remove from sleeping threads and unblock(to be placed at the ready queue) */
    list_remove(&t->elem);
    t->elapsed_sleep_time=0;
    t->sleep_time=0;
    t->status =  THREAD_READY ;
    thread_place_on_ready_list_per_sched_policy( &t->elem );
    ready_threads = ADD_INT_TO_FIXED_POINT_VALUE(ready_threads, 1);
  }
  return next_elem;
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();
  int64_t timer_ticks_since_os_booted = timer_ticks() ;
  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
  {
    kernel_ticks++;
  }

  if( thread_mlfqs )
  {
    /* update recent_cpu for current thread */
    if(t!=idle_thread)
    {
      t->recent_cpu = ADD_INT_TO_FIXED_POINT_VALUE((t->recent_cpu), 1);
    }
    if( timer_ticks_since_os_booted % TIMER_FREQ == 0 )
    {
      /* update mlfqs load_average */
      compute_load_average_for_mlfqs( );

      /* update recent_cpu for all threads */
      thread_foreach(thread_compute_mlfqs_recent_cpu, NULL);
    }

    if( timer_ticks_since_os_booted % TIME_SLICE == 0 )
    {
      /* recompute priority for every thread whose recent_cpu value has changed */
      thread_foreach(thread_recompute_priority_if_recent_cpu_changed, NULL);
    }
  }

  /* check to see if sleeping threads need to be woken up*/
  if(!list_empty(&sleeping_threads))
  {
    thread_foreach_inlist(&sleeping_threads, wakeup_sleeping_thread_if_its_time_return_next, NULL);  
  } 
  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock (t);

  return tid;
}

/* Puts the current thread to sleep, while it's waiting on a resource.  
   It will not be scheduled again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);
  if( running_thread()!=idle_thread )
  {
    ready_threads = SUBTRACT_INT_FROM_FIXED_POINT_VALUE(ready_threads, 1);
  }
  
  
  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Puts the current thread to sleep while its timer expires.  
   It will not be scheduled again until awoken by thread_unblock().
   This function must be called with interrupts turned off.
*/
void
thread_sleep(void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);
  
  //lock_acquire( &ready_threads_lock );
  
  //lock_release( &ready_threads_lock );
  thread_current ()->status = THREAD_SLEEPING;

  schedule();
}


/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED||t->status == THREAD_SLEEPING);
  struct thread *cur = thread_current ();
  // during init; this code will be executed before the idle_thread's function handler executes and sets the idle thread variable
  // this is fine. 
  if(cur!=idle_thread)
  {
    ready_threads = ADD_INT_TO_FIXED_POINT_VALUE(ready_threads, 1);
  }
  
  thread_place_on_ready_list_per_sched_policy(&t->elem);
  t->status = THREAD_READY;
  if(cur->priority<=t->priority)
  {
    if (cur != idle_thread) 
    {
      thread_place_on_ready_list_per_sched_policy(&cur->elem);
      cur->status = THREAD_READY;
      schedule ();
    }
  }
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif
  //lock_acquire(&ready_threads_lock);
  //lock_release(&ready_threads_lock);
  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  ready_threads = SUBTRACT_INT_FROM_FIXED_POINT_VALUE(ready_threads, 1);

  //TODO: Is a lock needed here?
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
    thread_place_on_ready_list_per_sched_policy(&cur->elem);
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

bool is_thread_from_list_elemA_high_priority(const struct list_elem* list_elemA, const struct list_elem* list_elemB, void* aux)
{
  struct thread * threadA= list_entry(list_elemA, struct thread, elem);
  struct thread * threadB= list_entry(list_elemB, struct thread, elem);
  uint8_t priority_threadA=threadA->priority;
  uint8_t priority_threadB=threadB->priority;
  bool return_val=false;

  if(priority_threadA>priority_threadB)
  {
    return_val=true;
  }

  return return_val;
}


void thread_place_on_ready_list_per_sched_policy(struct list_elem* thread)
{
  struct thread * thread_ptr;
  int32_t priority_after_rounding ;
  if( thread_mlfqs == false )
  {
    list_insert_ordered(&ready_list, thread, is_thread_from_list_elemA_high_priority, NULL);
  }
  else
  {
    // TODO:
    // special handling for ready list for mlfq case
    thread_ptr = list_entry(thread, struct thread, elem);
    // if value is negative, clamp to 0
    // if value after rounding is greater than PRI_MAX, clamp to PRI_MAX
    if ( thread_ptr->priority < 0 )
    {
      priority_after_rounding = 0;
    }
    else
    {
      // TODO: race condition investigation
      priority_after_rounding = GET_POSITIVE_INTEGER_FROM_FIXED_POINT( thread_ptr->priority ) ;
      
      if ( priority_after_rounding > PRI_MAX )
      {
        priority_after_rounding = PRI_MAX;
      }
    }
    list_push_back(&ready_list_mlfqs[priority_after_rounding], thread);
  }
}


/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void thread_foreach_inlist(struct list* thread_list, thread_action_func_return_next *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);
  e = list_begin (thread_list);
  while( e != list_end (thread_list))
  {
    struct thread *t = list_entry (e, struct thread, elem);
    e=func (t, aux);
  }
}

/* Sets the current thread's priority to NEW_PRIORITY. NOP for mlfqs */
void
thread_set_priority (int new_priority) 
{
  if( thread_mlfqs == false )
  {
    // check to see if priority donation is in progress
    if(thread_current ()->priority != thread_current ()->original_priority)
    {
      thread_current ()->original_priority = new_priority ; 
    }
    else
    {
      // reset both priority and original priority
      // this is saying this is an all new thread with the effective priority of 'new_priority'
      // else this runs into issues where the priority donation engine assumes there's been a
      // donation when in fact there is none.
      thread_current ()->priority = new_priority;
      thread_current ()->original_priority = new_priority;
      //check to see if other run-able candidates exist
      if( !list_empty(&ready_list) )
      {
        struct list_elem* front_elem=list_front(&ready_list);
        struct thread* highest_priority_thread_on_queue=list_entry(front_elem, struct thread, elem);
        
        //printf("highest_priority_thread_on_queuepriority%u, new_priority:%u\n",highest_priority_thread_on_queue->priority,new_priority);
        if(highest_priority_thread_on_queue->priority>new_priority)
        {
          thread_yield();
        }
      }
    }
  }
  // NOP for mlfqs
  
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* Sets the current thread's nice value to new nice and recalculates the thread's priority
based on the new value (see Section B.2 [Calculating Priority], page 91). If the
running thread no longer has the highest priority, yields.*/
void
thread_set_nice (int nice) 
{
  struct thread* current_thread= thread_current();

  current_thread->nice_value = (int8_t)nice;
  uint8_t current_priority =  current_thread->priority; 
  thread_compute_mlfqs_priority( current_thread );
  uint8_t new_priority = current_thread->priority;
  if(new_priority < current_priority)
  {
    thread_yield();
  }
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  struct thread* current_thread= thread_current();

  return current_thread->nice_value;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  // multiply fixed point value by 100 before converting to integer 
  // as opposed to conversion first and then multiplication
  // this helps us retain precision  
  return GET_POSITIVE_INTEGER_FROM_FIXED_POINT((load_average*100));
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu(void) 
{
  int32_t thread_recent_cpu_fixed_point = thread_current()->recent_cpu;
  return ( thread_recent_cpu_fixed_point >= 0 ) ? GET_POSITIVE_INTEGER_FROM_FIXED_POINT((thread_recent_cpu_fixed_point*100)) : GET_NEGATIVE_INTEGER_FROM_FIXED_POINT((thread_recent_cpu_fixed_point*100));
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

void thread_recompute_priority_if_recent_cpu_changed( struct thread* thread_ptr, void* aux UNUSED )
{
  if(thread_ptr->recent_cpu!=thread_ptr->recent_cpu_old)
  {
    thread_compute_mlfqs_priority(thread_ptr);
    thread_ptr->recent_cpu_old = thread_ptr->recent_cpu;
  }
}

/* function to compute mlfqs priority for thread */
void thread_compute_mlfqs_priority( struct thread* thread_ptr )
{ 
  /* Priority computation formula:  PRIORITY_MAX             -  recent_cpu/4                 -  nice x 2            */
  /* retain priority in fixed point; convert to int when accessing the list ; this saves cycles */
  thread_ptr->priority = ( GET_FIXED_POINT_OF_NUM(PRI_MAX) ) - (thread_ptr->recent_cpu >> 2) - (GET_FIXED_POINT_OF_NUM(thread_ptr->nice_value) << 1);
}



/* function to compute recent_cpu for thread */
void thread_compute_mlfqs_recent_cpu( struct thread* thread_ptr, void* AUX UNUSED )
{
  //int32_t recent_cpu_coefficient_fixed_pt = DIVIDE_FIXED_POINT_VALUES( ( load_average<<1 ), (ADD_INT_TO_FIXED_POINT_VALUE( ( load_average << 1 ), 1 )) );
  // TODO: try to keep it cleaner to see assembly generated; then optimize
  int32_t recent_cpu_times_twice_load_avg = MULTIPLY_FIXED_POINT_VALUES( (load_average<<1), thread_ptr->recent_cpu );
  thread_ptr->recent_cpu = ADD_INT_TO_FIXED_POINT_VALUE( ( DIVIDE_FIXED_POINT_VALUES( ( recent_cpu_times_twice_load_avg ), ( ADD_INT_TO_FIXED_POINT_VALUE( (load_average<<1) , 1 ) ) ) ), (thread_ptr->nice_value) ) ;
}

/* function to compute recent_cpu for thread */
void compute_load_average_for_mlfqs( void )
{
  //static uint32_t index;
  load_average = ((59*load_average) + ready_threads)/60;
  
    /*load_average_record[0][index]=1;
  load_average_record[1][index]=load_average;
  index++;*/  
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;
  struct thread* current_thread;
  
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  
  if( thread_mlfqs == false )
  {  //enable only for priority scheduling
    t->donee_priority = 0xFF;
    t->original_priority = priority;
    t->number_of_donors = 0;
    //t->donee_status = PRIORITY_NON_DONEE;
    t->donee_thread=NULL;
  }
  else
  {
    current_thread = running_thread(); 
    // recent_cpu and nice value for initial thread is 0; other threads inherit these value from parents
    if( t == initial_thread )
    {
      t->nice_value = 0;
      t->recent_cpu = 0;
    }
    else
    {
      t->nice_value = current_thread->nice_value ;
      t->recent_cpu = current_thread->recent_cpu ;
    }

    // now compute priority
    thread_compute_mlfqs_priority( t ); 
  }

  t->magic = THREAD_MAGIC;
  //list_init (&t->priority_donors_list);

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  int8_t ready_list_mlfqs_index;
  struct thread * next_thread;
  bool ready_list_nonempty = false ; 
  if ( thread_mlfqs )
  {
    // traverse through the list array to find first non empty list entry starting from PRI_MAX
    for( ready_list_mlfqs_index =  PRI_MAX; ready_list_mlfqs_index >=0 ; ready_list_mlfqs_index-- )
    {
      if( !list_empty(&ready_list_mlfqs[ready_list_mlfqs_index]) )
      {
        ready_list_nonempty = true;
        break;
      }
    }

    if( ready_list_nonempty )
    {
      next_thread = list_entry (list_pop_front (&ready_list_mlfqs[ready_list_mlfqs_index]), struct thread, elem);
    }
    else
    {

      next_thread = idle_thread;
    }

  }
  else
  {
    if (list_empty (&ready_list))
    {
      next_thread = idle_thread;
    }
    else 
    {
      next_thread = list_entry (list_pop_front (&ready_list), struct thread, elem);
    }
  }
  return next_thread;
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
