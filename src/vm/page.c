#include "vm/page.h"
#include "threads/vaddr.h"

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
