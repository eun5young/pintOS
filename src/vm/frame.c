#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "vm/swap.h"
#include "vm/page.h"
#include "lib/kernel/hash.h"
#include "lib/kernel/list.h"

static struct hash frame_table;
struct hash frame_map;              // Frame table (hash table)
struct list frame_list;             // Frame list (for eviction)
struct lock frame_lock;             // Global frame lock
struct list_elem *clock_ptr;        // Clock algorithm pointer

static unsigned frame_hash(const struct hash_elem *e, void *aux UNUSED) {
    const struct frame_table_entry *fte = hash_entry(e, struct frame_table_entry, helem);
    return hash_bytes(&fte->kpage, sizeof fte->kpage);
}

static bool frame_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED) {
    const struct frame_table_entry *fte_a = hash_entry(a, struct frame_table_entry, helem);
    const struct frame_table_entry *fte_b = hash_entry(b, struct frame_table_entry, helem);
    return fte_a->kpage < fte_b->kpage;
}

void frame_init(void) {
    hash_init(&frame_table, frame_hash, frame_less, NULL);
    lock_init(&frame_lock);
}

void *frame_allocate(enum palloc_flags flags, void *upage) {
    lock_acquire(&frame_lock);

    void *kpage = palloc_get_page(flags);
    if (kpage == NULL) {
        // eviction 필요 (추후 구현 예정)
        lock_release(&frame_lock);
        return NULL;
    }

    struct frame_table_entry *fte = malloc(sizeof(struct frame_table_entry));
    if (fte == NULL) {
        palloc_free_page(kpage);
        lock_release(&frame_lock);
        return NULL;
    }

    fte->kpage = kpage;
    fte->upage = upage;
    fte->t = thread_current();
    fte->pinned = false;

    hash_insert(&frame_table, &fte->helem);

    lock_release(&frame_lock);
    return kpage;
}

void frame_do_free(void *kpage, bool free_page) {
    lock_acquire(&frame_lock);

    struct frame_table_entry fte_temp;
    fte_temp.kpage = kpage;
    struct hash_elem *e = hash_find(&frame_table, &fte_temp.helem);

    if (e != NULL) {
        struct frame_table_entry *fte = hash_entry(e, struct frame_table_entry, helem);
        hash_delete(&frame_table, e);
        free(fte);
    }

    if (free_page) {
        palloc_free_page(kpage);
    }

    lock_release(&frame_lock);
}


void frame_set_pinned(void *kpage, bool pinned) {
    lock_acquire(&frame_lock);

    struct frame_table_entry temp;
    temp.kpage = kpage;

    struct hash_elem *e = hash_find(&frame_table, &temp.helem);
    if (e != NULL) {
        struct frame_table_entry *fte = hash_entry(e, struct frame_table_entry, helem);
        fte->pinned = pinned;
    }

    lock_release(&frame_lock);
}

void *pick_frame_to_evict(void) {
    struct hash_iterator i;
    hash_first(&i, &frame_map);

    size_t max_iter = hash_size(&frame_map) * 2;
    size_t cnt;
    for (cnt = 0; cnt < max_iter; cnt++) {
        if (!hash_next(&i)) {
            hash_first(&i, &frame_map); // 다시 처음부터
            continue;
        }

        struct frame_table_entry *fte = hash_entry(hash_cur(&i), struct frame_table_entry, helem);

        if (fte->pinned) continue;

        if (pagedir_is_accessed(fte->t->pagedir, fte->upage)) {
            pagedir_set_accessed(fte->t->pagedir, fte->upage, false);
        } else {
            return fte->kpage;
        }
    }

    PANIC("No frame to evict!");
}
