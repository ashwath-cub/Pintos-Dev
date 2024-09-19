#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"
#include <list.h>
typedef struct 
{
    tid_t exec_child_tid;
    struct semaphore* exec_syncer;
}exec_notification;

typedef struct
{
    const char* cmd_line;
    exec_notification* notify;
}task_details;

tid_t process_execute (task_details* details);

int process_wait (tid_t, struct list*);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
