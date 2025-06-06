#include "vm/page.h"
#include "threads/vaddr.h"
#include "vm/swap.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include <string.h>   // memset, memcpy 등


struct supplemental_page_table_entry *spt_find(struct hash *spt, void *upage);
bool spt_insert(struct hash *spt, struct supplemental_page_table_entry *spte);
void *frame_allocate(enum palloc_flags flags, void *upage);
void frame_do_free(void *kpage);



unsigned page_hash(const struct hash_elem *e, void *aux UNUSED) {
  const struct supplemental_page_table_entry *spte = 
      hash_entry(e, struct supplemental_page_table_entry, elem);
  return hash_bytes(&spte->upage, sizeof spte->upage);
}

bool page_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED) {
  const struct supplemental_page_table_entry *spte_a = 
      hash_entry(a, struct supplemental_page_table_entry, elem);
  const struct supplemental_page_table_entry *spte_b = 
      hash_entry(b, struct supplemental_page_table_entry, elem);
  return spte_a->upage < spte_b->upage;
}

void spt_init(struct hash *spt) {
  hash_init(spt, page_hash, page_less, NULL);
}

struct supplemental_page_table_entry *spt_find(struct hash *spt, void *upage) {
  struct supplemental_page_table_entry temp;
  temp.upage = pg_round_down(upage);
  struct hash_elem *e = hash_find(spt, &temp.elem);
  return e ? hash_entry(e, struct supplemental_page_table_entry, elem) : NULL;
}

void spt_destroy_func(struct hash_elem *e, void *aux UNUSED) {
  struct supplemental_page_table_entry *spte = 
      hash_entry(e, struct supplemental_page_table_entry, elem);
  free(spte);
}

void spt_destroy(struct hash *spt) {
  hash_destroy(spt, spt_destroy_func);
}



bool vm_load_page(struct supplemental_page_table_entry *spte) {
    ASSERT(spte != NULL);

    // 1. frame 할당
    void *kpage = frame_allocate(PAL_USER, spte->upage);
    if (kpage == NULL) return false;

    // 2. 페이지 상태에 따라 로딩 방법 결정
    switch (spte->status) {
        case ALL_ZERO:
            memset(kpage, 0, PGSIZE);
            break;

        case ON_SWAP:
            if (!swap_in(spte->swap_index, kpage))
                return false;
            break;

        case FROM_FILESYS:
            if (!install_page(spte->upage, kpage, spte->writable)) {
                frame_do_free(kpage);
                return false;
                }

            break;

        case ON_FRAME:
            return true;  // 이미 로딩된 경우
    }

    // 3. 매핑 및 상태 업데이트
    if (!install_page(spte->upage, kpage, spte->writable)) {
        frame_do_free(kpage);
        return false;
    }

    spte->kpage = kpage;
    spte->status = ON_FRAME;
    return true;
}

bool vm_load_page_from_filesys(struct supplemental_page_table_entry *spte, void *kpage) {
    ASSERT(spte != NULL);
    ASSERT(spte->file != NULL);

    off_t result = file_read_at(spte->file, kpage, spte->read_bytes, spte->file_offset);
    if (result != (int)spte->read_bytes) {
        return false;
    }

    memset(kpage + spte->read_bytes, 0, spte->zero_bytes);
    return true;
}

bool spt_insert(struct hash *spt, struct supplemental_page_table_entry *spte) {
    ASSERT(spt != NULL);
    ASSERT(spte != NULL);

    struct hash_elem *prev = hash_insert(spt, &spte->elem);
    return prev == NULL;  // 중복이 없으면 성공
}


bool
spt_install_filesys(struct file *file, off_t ofs, uint8_t *upage,
                    uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
    ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
    ASSERT(pg_ofs(upage) == 0);
    ASSERT(ofs % PGSIZE == 0);

    while (read_bytes > 0 || zero_bytes > 0) {
        size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;

        struct supplemental_page_table_entry *spte = malloc(sizeof *spte);
        if (!spte) return false;

        spte->upage = upage;
        spte->kpage = NULL;
        spte->status = FROM_FILESYS;
        spte->file = file;
        spte->file_offset = ofs;
        spte->read_bytes = page_read_bytes;
        spte->zero_bytes = page_zero_bytes;
        spte->writable = writable;

        if (!spt_insert(&thread_current()->spt, spte)) {
            free(spte);
            return false;
        }

        // advance
        read_bytes -= page_read_bytes;
        zero_bytes -= page_zero_bytes;
        ofs += page_read_bytes;
        upage += PGSIZE;
    }
    return true;
}
