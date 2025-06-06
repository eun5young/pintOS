#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <hash.h>
#include <list.h>
#include <stdbool.h>
#include "threads/thread.h"
#include "threads/palloc.h"


struct frame_table_entry {
    void *kpage;                  // 물리 주소
    struct hash_elem helem;      // hash table 요소
    struct list_elem lelem;      // frame list 요소
    void *upage;                 // 매핑된 가상 주소
    struct thread *t;           // 이 frame을 소유한 thread
    bool pinned;                // true면 스왑 금지
};

void frame_init(void);
void *frame_allocate(enum palloc_flags, void *upage);
void frame_do_free(void *kpage, bool free_page);

#endif
