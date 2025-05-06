#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

/*2-2*/
#include <stdint.h>
typedef int pid_t;

#include "threads/interrupt.h"
#include <stdbool.h>
/*2-2*/

void syscall_init (void);

/* 2-2 구현 함수들 선언 */
bool is_valid_ptr(const void *usr_ptr);
void halt(void);
void exit(int status);
int wait(pid_t pid);
/*2-2*/

#endif /* userprog/syscall.h */
