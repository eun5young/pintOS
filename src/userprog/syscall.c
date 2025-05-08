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


static void syscall_handler (struct intr_frame *);
/*2-2*/
typedef int pid_t;
/*2-2*/
/*2-3*/
static struct list open_files;
static struct lock fs_lock;
// syscall_handler 위쪽에 추가
static bool is_valid_buffer(const void *buffer, unsigned size);

/*2-3*/
struct file_descriptor {
  int fd_num;                    // 파일 디스크립터 번호
  tid_t owner;                   // 소유한 스레드 ID
  struct file *file_struct;      // 실제 파일 객체를 가리키는 포인터
  struct list_elem elem;         // 열린 파일 리스트에 포함되기 위한 요소
};

struct file_descriptor *get_open_file(int fd) {
  struct list_elem *e;

  for (e = list_begin(&open_files); e != list_end(&open_files); e = list_next(e)) {
    struct file_descriptor *fd_entry = list_entry(e, struct file_descriptor, elem);

    // 현재 스레드가 소유한 파일인지도 확인
    if (fd_entry->fd_num == fd && fd_entry->owner == thread_current()->tid) {
      return fd_entry;
    }
  }
  return NULL;  // 못 찾으면 NULL
}
/*2-3*/

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  /*2-3*/
  list_init(&open_files);
  lock_init(&fs_lock);  // 파일 시스템 보호용 락
  /*2-3*/
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  // printf ("system call!\n");
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

    /*2-3*/
    case SYS_WRITE: {
      if (!is_valid_ptr(f->esp + 4) || !is_valid_ptr(f->esp + 8) || !is_valid_ptr(f->esp + 12)) {
        exit(-1);
      }

      int fd = *(int *)(f->esp + 4);
      const void *buffer = *(void **)(f->esp + 8);
      unsigned size = *(unsigned *)(f->esp + 12);

      if (!is_valid_buffer(buffer, size)) {
        exit(-1);
      }

      f->eax = sys_write(fd, buffer, size);
      break;
    }
    /*2-3*/

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

/*2-3*/

int sys_write(int fd, const void *buffer, unsigned size) {
  // 1. 포인터 유효성 검사
  if (!is_valid_ptr(buffer)) {
    exit(-1);
  }

  // 2. 파일 시스템 락 획득
  lock_acquire(&fs_lock);

  int status = -1;

  // 3. stdin이면 잘못된 요청
  if (fd == STDIN_FILENO) {
    lock_release(&fs_lock);
    return -1;
  }

  // 4. stdout이면 putbuf 사용
  if (fd == STDOUT_FILENO) {
    putbuf(buffer, size);
    lock_release(&fs_lock);
    return size;
  }

  // 5. 나머지 파일 디스크립터에 대해 파일 객체 찾기
  struct file_descriptor *fd_struct = get_open_file(fd);
  if (fd_struct != NULL && fd_struct->file_struct != NULL) {
    status = file_write(fd_struct->file_struct, buffer, size);
  }

  // 6. 락 해제 및 결과 반환
  lock_release(&fs_lock);
  return status;
}

static int allocate_fd(struct thread *t) {
  static int next_fd = 3; // 0=stdin, 1=stdout, 2=stderr 예약됨
  return next_fd++;
}
/*2-3*/

/*2-3*/
// size만큼 연속된 버퍼의 모든 주소가 유효한지 확인하는 함수
static bool is_valid_buffer(const void *buffer, unsigned size) {
  const uint8_t *addr = (const uint8_t *)buffer;
  unsigned i;
  for (i = 0; i < size; i++) {
    if (!is_valid_ptr(addr + i)) {
      return false;
    }
  }
  return true;
}