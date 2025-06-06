#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "lib/kernel/hash.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "filesys/file.h"

typedef int off_t;  // 여기가 핵심!

void spt_init(struct hash *spt);
void spt_destroy(struct hash *spt);

bool vm_load_page_from_filesys(struct supplemental_page_table_entry *spte, void *kpage);



enum page_status {
  ALL_ZERO,     // 0으로 채울 페이지
  ON_FRAME,     // 물리 메모리에 있음
  ON_SWAP,      // 스왑에 있음
  FROM_FILESYS  // 파일에서 로딩 예정
};

struct supplemental_page_table_entry {
  void *upage;        // 가상 주소
  void *kpage;        // 현재 할당된 물리 주소 (없으면 NULL)
  struct hash_elem elem;

  enum page_status status;
  size_t swap_index;
  bool dirty;

  struct file *file;  // 파일 매핑일 경우
  off_t file_offset;
  uint32_t read_bytes;
  uint32_t zero_bytes;
  bool writable;
};

#endif