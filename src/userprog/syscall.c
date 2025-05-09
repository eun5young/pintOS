#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
/*2-2: valid*/
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
/*2-2: halt*/
#include "devices/shutdown.h"
/*2-2: wait*/
#include "userprog/process.h"

#include <stdint.h>
/*2-3*/
#include "threads/synch.h"
#include "lib/kernel/console.h"

#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/palloc.h"
#include "lib/string.h"
#include <stdlib.h>
#include "devices/input.h"


static void syscall_handler (struct intr_frame *);
/*2-2*/
typedef int pid_t;
/*2-2*/
/*2-3*/
/* 파일 시스템 락과 파일 테이블 초기화 */
struct lock filesys_lock;
static struct list file_table;

/*2-3-1*/
/* file descriptor 구조체 정의 (슬라이드 반영) */
struct file_descriptor {
  int fd_num;                    // 파일 디스크립터 번호
  tid_t owner;                  // 소유한 스레드 ID
  struct file *file_struct;     // 실제 파일 객체
  struct list_elem elem;        // 리스트 요소
};
/*2-3-1*/

static void *user_to_kernel_vaddr(void *uaddr); // 커널 주소로 변환 함수
static pid_t exec(const char *file);            // exec 함수
/*2-3-1*/
static struct file_descriptor *get_open_file(int fd);


/*2-3*/

/* 시스템 콜 초기화 함수 */
void syscall_init (void) {
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&filesys_lock);
  list_init (&file_table);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  // printf ("system call!\n");
  // thread_exit ();

  /*2-2*/
  void *esp = f->esp;

  // 1. 스택 포인터 유효성 검사
  if (!is_valid_ptr(esp)) exit(-1);

  // 2. 시스템 콜 번호 추출
  int syscall_number = *(int *)esp;

  // 3. 시스템 콜 인자 처리 방식 (슬라이드에 맞게 직접 계산)
  int arg0, arg1, arg2;
  if (syscall_number >= 1 && syscall_number <= 12) {
    if (!is_valid_ptr((int *)esp + 1)) exit(-1);
    arg0 = *(((int *)esp) + 1);
  }
  if (syscall_number >= 3 && syscall_number <= 12) {
    if (!is_valid_ptr((int *)esp + 2)) exit(-1);
    arg1 = *(((int *)esp) + 2);
  }
  if (syscall_number >= 9 && syscall_number <= 10) {
    if (!is_valid_ptr((int *)esp + 3)) exit(-1);
    arg2 = *(((int *)esp) + 3);
  }

  // 4. 시스템 콜 처리
  switch (syscall_number) {
    case SYS_HALT:
      halt();
      break;
    case SYS_EXIT:
      exit(arg0);
      break;
    case SYS_EXEC:
      f->eax = exec((const char *)user_to_kernel_vaddr((void *)arg0));
      break;
    case SYS_WAIT:
      f->eax = wait((pid_t)arg0);
      break;
    /*2-3-1*/
    case SYS_WRITE:
      f->eax = write(arg0, (const void *)arg1, (unsigned)arg2);
      break;
    default:
      exit(-1);
  }
  /*2-2*/
}

/*2-2*/
/* 유저 포인터가 유효한지 검사하는 함수 (슬라이드에 맞게 구현) */
bool is_valid_ptr(const void *usr_ptr) {
  struct thread *cur = thread_current();
  if (usr_ptr == NULL) return false;
  if (!is_user_vaddr(usr_ptr)) return false;
  if (pagedir_get_page(cur->pagedir, usr_ptr) == NULL) return false;
  return true;
}

/* 유저 주소를 커널 주소로 변환하며 유효성 검사 포함 */
static void *user_to_kernel_vaddr(void *uaddr) {
  if (!is_valid_ptr(uaddr)) exit(-1);
  return pagedir_get_page(thread_current()->pagedir, uaddr);
}
/*2-2*/

/*2-3-1*/
/* file descriptor로부터 file_descriptor 구조체를 찾음 */
static struct file_descriptor *get_open_file(int fd) {
  struct list_elem *e;
  for (e = list_begin(&file_table); e != list_end(&file_table); e = list_next(e)) {
    struct file_descriptor *fdt = list_entry(e, struct file_descriptor, elem);
    if (fdt->fd_num == fd && fdt->owner == thread_current()->tid)
      return fdt;
  }
  return NULL;
}
/*2-3-1*/

/* Pintos 종료 */
void halt(void) {
  shutdown_power_off();
}

/* 현재 프로세스를 종료하고 상태를 부모에게 전달 */
void exit(int status) {
  struct thread *cur = thread_current();
  // if (cur->child_elem != NULL)
  //   cur->child_elem->exit_status = status;
  printf("%s: exit(%d)\n", cur->name, status);
  thread_exit();
}

/* 새로운 프로세스 실행 */
pid_t exec(const char *file) {
  file = user_to_kernel_vaddr((void *)file);
  return process_execute(file);
}

/* 자식 프로세스 종료 대기 */
int wait(pid_t pid) {
  return process_wait(pid);
}

/*2-3*/

/*2-3-1*/
/* write syscall 구현 (슬라이드 순서에 따라) */
int write(int fd, const void *buffer, unsigned size) {
  // 1. 포인터 유효성 검사
  if (!is_valid_ptr(buffer)) exit(-1);
  
  // 2. 파일 시스템 락 획득
  lock_acquire(&filesys_lock);

  // 3. 표준 입력 검사
  if (fd == STDIN_FILENO) {
    lock_release(&filesys_lock);
    return -1;
  }

  // 4. 표준 출력인 경우 처리
  if (fd == STDOUT_FILENO) {
    putbuf(buffer, size);
    lock_release(&filesys_lock);
    return size;
  }

  // 5. 일반 파일에 대한 처리
  struct file_descriptor *fdt = get_open_file(fd);
  if (fdt == NULL || fdt->file_struct == NULL) {
    lock_release(&filesys_lock);
    return -1;
  }
  int bytes_written = file_write(fdt->file_struct, buffer, size);

  // 6. 락 해제 후 결과 반환
  lock_release(&filesys_lock);
  return bytes_written;
}
/*2-3-1*/

