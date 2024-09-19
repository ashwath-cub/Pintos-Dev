#include "userprog/syscall.h"
#include "userprog/process.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

static void syscall_handler (struct intr_frame *);

#define LAST_SYSTEM_CALL_NUMBER               SYS_INUMBER
#define NUMBER_OF_SYSTEM_CALLS                LAST_SYSTEM_CALL_NUMBER + 1 // plus 1 to account for zero indexing           
#define NUMBER_OF_ARGS_IN_SYSTEM_CALL3_TYPE   3
#define SIZE_OF_SYSTEM_CALL_NUMBER            sizeof(int)
#define SIZE_OF_LAST_ARGUMENT_PUSHED_BY_USER  SIZE_OF_SYSTEM_CALL_NUMBER
#define M_get_pointer_to_system_call_number_from_user_stack_ptr( user_stack_pointer )        ( user_stack_pointer )     

#define M_exit_if_user_addr_is_invalid(x) \
do\
{\
  if( (!is_user_vaddr(x)) || ((x)==NULL) ) \
  { \
    /* kill user process */\
    thread_exit(-1); \
  }\
}while(0)


#define M_get_args_base_on_user_stack_for_sys_call( user_stack_pointer )               ( (uint8_t*)user_stack_pointer + ( SIZE_OF_LAST_ARGUMENT_PUSHED_BY_USER ) ) // system call number is the last arg that gets pushed

typedef struct
{
  int fd; 
  const void *buffer; 
  unsigned size;
}write_sys_call_args;

typedef struct
{
  int fd; 
  void *buffer; 
  unsigned size;
}read_sys_call_args;


typedef struct 
{
  int exit_status;
}exit_sys_call_arg;

typedef struct 
{
  const char *file;
}open_sys_call_arg;

typedef struct 
{
  const char *file;
}remove_sys_call_arg;

typedef struct 
{
  const char *file;
  off_t initial_size;
}create_sys_call_arg;

typedef struct 
{
  const char *cmd_line;
}exec_sys_call_arg;

typedef struct 
{
  tid_t child_tid;
}wait_sys_call_arg;

typedef struct 
{
  int file_descriptor;
}filesize_sys_call_arg;

typedef struct 
{
  int file_descriptor;
}tell_sys_call_arg;

typedef struct 
{
  int file_descriptor;
  unsigned position;
}seek_sys_call_arg;


typedef struct 
{
  int file_descriptor;
}close_sys_call_arg;


static uint32_t write_system_call_handler( void* start_of_user_args);
static uint32_t default_system_call_handler( void* start_of_user_args );
static uint32_t exit_system_call_handler( void* start_of_user_args );
static uint32_t halt_system_call_handler( void* start_of_user_args );
static uint32_t open_system_call_handler( void* start_of_user_args );
static uint32_t create_system_call_handler( void* start_of_user_args );
static uint32_t read_system_call_handler( void* start_of_user_args );
static uint32_t remove_system_call_handler( void* start_of_user_args );
static uint32_t seek_system_call_handler( void* start_of_user_args );
static uint32_t tell_system_call_handler( void* start_of_user_args );
static uint32_t close_system_call_handler( void* start_of_user_args );
static uint32_t filesize_system_call_handler( void* start_of_user_args );
static uint32_t exec_system_call_handler( void* start_of_user_args );
static uint32_t wait_system_call_handler( void* start_of_user_args );

//typedef void intr_handler_func (struct intr_frame *);
typedef uint32_t syscall_handler_func( void* start_of_user_args );

static syscall_handler_func *syscall_handler_array[NUMBER_OF_SYSTEM_CALLS]=
{
  halt_system_call_handler, // 0
  exit_system_call_handler, // 1
  exec_system_call_handler, // 2
  wait_system_call_handler, // 3
  create_system_call_handler, // 4
  remove_system_call_handler, // 5
  open_system_call_handler, // 6
  filesize_system_call_handler, // 7
  read_system_call_handler, // 8
  write_system_call_handler, // 9
  seek_system_call_handler, // 10
  tell_system_call_handler, // 11
  close_system_call_handler, // 12
  default_system_call_handler, // 13
  default_system_call_handler, // 14
  default_system_call_handler, // 15
  default_system_call_handler, // 16
  default_system_call_handler, // 17
  default_system_call_handler, // 18
  default_system_call_handler, // 19
};

/*shared between open, create, remove, and close*/
static struct lock filesys_lck;
/*shared between read, write and length*/
static struct lock filesys_lck2;



void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&filesys_lck);
  lock_init(&filesys_lck2);
}

static void
syscall_handler (struct intr_frame *f) 
{
  uint32_t* system_call_number_ptr;
  uint32_t syscall_return;
  system_call_number_ptr = (uint32_t*)M_get_pointer_to_system_call_number_from_user_stack_ptr(f->esp);
  void* user_args_base = M_get_args_base_on_user_stack_for_sys_call(f->esp);
  
  M_exit_if_user_addr_is_invalid(system_call_number_ptr);
  M_exit_if_user_addr_is_invalid(user_args_base);
  
  syscall_return = syscall_handler_array[*system_call_number_ptr](user_args_base);
  f->eax = syscall_return;
  //printf ("system call number:%d received!\n", *system_call_number_ptr);
}

static struct file* return_file_ptr_for_valid_process_fd( int file_descriptor )
{
  struct thread* current_thread;
  struct file* file_ptr = NULL; 

  if( file_descriptor >= MIN_USER_FD_NUMBER && file_descriptor <= LAST_INDEX_IN_ARRAY_STORING_MAX_WORDS_IN_4K_PAGE )
  {
    current_thread = thread_current();

    if( current_thread->file_descriptor_table[ file_descriptor ] )
    {
      file_ptr = (struct file*)current_thread->file_descriptor_table[ file_descriptor ];
    }
  }
  return file_ptr;
}

static uint32_t exec_system_call_handler( void* start_of_user_args )
{
  exec_sys_call_arg* exec_args = (exec_sys_call_arg*)start_of_user_args;
  M_exit_if_user_addr_is_invalid(exec_args->cmd_line);
  
  task_details details;
  exec_notification notification;
  
  struct semaphore syncer;
  sema_init(&syncer, 0);
  
  details.cmd_line = exec_args->cmd_line;
  notification.exec_syncer = &syncer;
  details.notify = &notification;
  
  process_execute(&details);

  sema_down(&syncer);

  return notification.exec_child_tid;
}

static uint32_t wait_system_call_handler( void* start_of_user_args )
{
  wait_sys_call_arg* wait_arg = (wait_sys_call_arg*)start_of_user_args;
  struct thread* current_thread = thread_current();
  return process_wait(wait_arg->child_tid, &current_thread->children);
}

static uint32_t halt_system_call_handler( void* start_of_user_args UNUSED )
{
  shutdown_power_off();
}

static uint32_t default_system_call_handler( void* start_of_user_args UNUSED )
{
  printf("hit default handler\n");
  while(1);
  return 0;
}

static uint32_t exit_system_call_handler( void* start_of_user_args )
{
  exit_sys_call_arg* exit_status = (exit_sys_call_arg*)start_of_user_args;

  /* call thread exit; it should check to see if parent is waiting */
  thread_exit( exit_status->exit_status );  
}

static uint32_t open_system_call_handler( void* start_of_user_args )
{
  open_sys_call_arg* open_args = (open_sys_call_arg*)start_of_user_args;
  uint32_t open_return = (uint32_t)-1;
  struct file* file_ptr=NULL;
  struct thread* current_thread = thread_current();

  M_exit_if_user_addr_is_invalid(open_args->file);
  
  if( current_thread->file_descriptor_ctr <= LAST_INDEX_IN_ARRAY_STORING_MAX_WORDS_IN_4K_PAGE )
  {
    lock_acquire(&filesys_lck);
    file_ptr = filesys_open(open_args->file);
    lock_release(&filesys_lck);

    if(file_ptr!=NULL)
    {
      
      current_thread->file_descriptor_table[ current_thread->file_descriptor_ctr ] = (uint32_t)file_ptr;
      open_return = current_thread->file_descriptor_ctr++;
    }
  }
  else
  {
    PANIC("too many fds open");
  }

  return open_return;
}

static uint32_t create_system_call_handler( void* start_of_user_args )
{
  create_sys_call_arg* create_args = (create_sys_call_arg*)start_of_user_args;
  M_exit_if_user_addr_is_invalid(create_args->file);
  M_exit_if_user_addr_is_invalid(create_args + sizeof(create_sys_call_arg) -sizeof(void*) );

  lock_acquire(&filesys_lck);
  uint32_t create_return=(uint32_t)filesys_create(create_args->file, create_args->initial_size);
  lock_release(&filesys_lck);
  return create_return;
}

static uint32_t read_system_call_handler( void* start_of_user_args )
{
  read_sys_call_args* read_args = (read_sys_call_args*)start_of_user_args;
  
  struct file* file_ptr;
  uint32_t read_return = (uint32_t)-1; 
  uint32_t input_idx=0;
  uint8_t* read_buffer = (uint8_t*)read_args->buffer;;
  
  M_exit_if_user_addr_is_invalid(read_args + sizeof(read_sys_call_args)-sizeof(void*));
  M_exit_if_user_addr_is_invalid( read_buffer + read_args->size );
  

  if(read_args->fd==0)
  {
    while( input_idx < read_args->size )
    {
      
      read_buffer[ input_idx ] = input_getc();
      input_idx++;
    }
    read_return = read_args->size;
      
  }
  else 
  {
    /* get corresponding file ptr based on fd */
    file_ptr = return_file_ptr_for_valid_process_fd(read_args->fd);
    
    if( file_ptr )
    {
      lock_acquire(&filesys_lck2);
      read_return = file_read(file_ptr, (void*)read_buffer, read_args->size);
      lock_release(&filesys_lck2);
    }
      
  }
  
  return read_return;
}

static uint32_t write_system_call_handler( void* start_of_user_args )
{
  write_sys_call_args* write_args = (write_sys_call_args*)start_of_user_args;
  
  struct file* file_ptr;
  uint32_t write_return = (uint32_t)-1; 
  uint8_t* write_buffer = (uint8_t*) write_args->buffer;

  M_exit_if_user_addr_is_invalid(write_args + sizeof(write_sys_call_args)-sizeof(void*));
  M_exit_if_user_addr_is_invalid( write_buffer + write_args->size );

  if( write_args->fd == 1 )
  {
    putbuf(write_args->buffer, write_args->size);
    write_return = write_args->size;
  }
  else
  {
    /* get corresponding file ptr based on fd */
    file_ptr = return_file_ptr_for_valid_process_fd(write_args->fd);
    
    if( file_ptr )
    {
      lock_acquire(&filesys_lck2);
      write_return = file_write(file_ptr, write_args->buffer, write_args->size);
      lock_release(&filesys_lck2);
    }

  }
  return write_return;
}


static uint32_t remove_system_call_handler( void* start_of_user_args )
{
  remove_sys_call_arg* remove_args = (remove_sys_call_arg*)start_of_user_args;

  M_exit_if_user_addr_is_invalid(remove_args->file);

  lock_acquire(&filesys_lck2);
  uint32_t remove_return = (uint32_t)filesys_remove(remove_args->file);;
  lock_release(&filesys_lck2);

  return remove_return;
}

static uint32_t seek_system_call_handler( void* start_of_user_args )
{
  seek_sys_call_arg* seek_args = (seek_sys_call_arg*)start_of_user_args;
  struct file* file_ptr;
  uint32_t seek_return = (uint32_t)-1;
  M_exit_if_user_addr_is_invalid(seek_args + sizeof(seek_sys_call_arg) -sizeof(void*));

  /* get corresponding file ptr based on fd */
  file_ptr = return_file_ptr_for_valid_process_fd(seek_args->file_descriptor);
  
  if( file_ptr )
  {
    file_seek( file_ptr, seek_args->position );
    seek_return = 0;
  }

  return seek_return;
}

static uint32_t tell_system_call_handler( void* start_of_user_args )
{
  tell_sys_call_arg* tell_args = (tell_sys_call_arg*)start_of_user_args;
  
  struct file* file_ptr;
  uint32_t tell_return = (uint32_t)-1;

  /* get corresponding file ptr based on fd */
  file_ptr = return_file_ptr_for_valid_process_fd(tell_args->file_descriptor); 

  if( file_ptr )
  {
    tell_return = file_tell( file_ptr);
  }
    

  return tell_return;
}

static uint32_t close_system_call_handler( void* start_of_user_args )
{
  close_sys_call_arg* close_args = (close_sys_call_arg*)start_of_user_args;
  struct file* file_ptr;
  struct thread* current_thread;
  
  uint32_t close_return = (uint32_t)-1;

  /* get corresponding file ptr based on fd */
  file_ptr = return_file_ptr_for_valid_process_fd(close_args->file_descriptor); 
  
  if(file_ptr)
  {
    current_thread =  thread_current();

    lock_acquire(&filesys_lck);
    file_close( file_ptr);
    lock_release(&filesys_lck);
    
    /* zero out entry in descriptor table */
    current_thread->file_descriptor_table[close_args->file_descriptor] = 0;
  }
    

  return close_return;
}

static uint32_t filesize_system_call_handler( void* start_of_user_args )
{
  filesize_sys_call_arg* filesize_args = (filesize_sys_call_arg*)start_of_user_args;
  
  struct file* file_ptr;
  uint32_t filesize_return = (uint32_t)-1;

  /* get corresponding file ptr based on fd */
  file_ptr = return_file_ptr_for_valid_process_fd(filesize_args->file_descriptor); 
  
  if(file_ptr)
  {
    lock_acquire(&filesys_lck2);
    filesize_return = file_length( file_ptr);
    lock_release(&filesys_lck2);
  }
  
  return filesize_return;
}


