#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

#define MIN_USER_FD_NUMBER    2

#define NUMBER_OF_WORDS_POSSIBLE_IN_4K_PAGE                 (1024)
#define LAST_INDEX_IN_ARRAY_STORING_MAX_WORDS_IN_4K_PAGE    (NUMBER_OF_WORDS_POSSIBLE_IN_4K_PAGE - 1)

#endif /* userprog/syscall.h */
