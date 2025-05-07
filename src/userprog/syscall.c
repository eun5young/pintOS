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


static void syscall_handler (struct intr_frame *);
/*2-2*/
typedef int pid_t;
/*2-2*/

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  printf ("system call!\n");
  // thread_exit ();

  /*2-2*/
  // 스택 포인터에서 시스템 콜 번호를 꺼낼 수 있어야 함
  if (!is_valid_ptr(f->esp)) {
    exit(-1);
  }

  int syscall_num = *(int *)(f->esp); // 스택의 첫 값: syscall 번호

  switch (syscall_num) {
    case SYS_HALT:
      halt();
      break;

    case SYS_EXIT: {
      if (!is_valid_ptr(f->esp + 4)) {
        exit(-1);
      }
      int status = *(int *)(f->esp + 4);  // 인자 1개
      exit(status);
      break;
    }

    case SYS_WAIT: {
      if (!is_valid_ptr(f->esp + 4)) {
        exit(-1);
      }
      pid_t pid = *(pid_t *)(f->esp + 4);
      f->eax = wait(pid);  // 결과 저장
      break;
    }

    // 향후 구현할 write 등은 여기에 추가
    default:
      printf("Unimplemented system call: %d\n", syscall_num);
      exit(-1);
  }
  /*2-2*/
}

/*2-2*/
// 포인터가 유효한 유저 포인터인지 검사하는 함수
bool is_valid_ptr(const void *usr_ptr) {
  // 1. NULL 체크
  if (usr_ptr == NULL) {
    return false;
  }

  // 2. 유저 영역 주소인지 확인 (PHYS_BASE는 커널과 유저 영역 경계)
  if (!is_user_vaddr(usr_ptr)) {
    return false;
  }

  // 3. 해당 주소가 실제로 매핑된 메모리인지 확인
  void *phys_addr = pagedir_get_page(thread_current()->pagedir, usr_ptr);
  if (phys_addr == NULL) {
    return false;
  }

  // 전부 통과했으면 유효한 포인터
  return true;
}
/*2-2*/

void halt(void) {
  shutdown_power_off();
}

void exit(int status) {
  struct thread *cur = thread_current();  // 현재 쓰레드 정보
  printf("%s: exit(%d)\n", cur->name, status);  // 로그 출력

  cur->exit_status = status;  // 종료 상태 저장 (process_wait에서 사용)
  thread_exit();  // 쓰레드 종료
}

int wait(pid_t pid) {
  return process_wait(pid);
}