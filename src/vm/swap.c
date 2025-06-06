#include "vm/swap.h"
#include <bitmap.h>
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "devices/block.h"
#include "lib/kernel/bitmap.h"
#include <stdio.h>
#include <string.h>

/* Global swap block and bitmap */
static struct block *swap_block;
static struct bitmap *swap_available;
static size_t swap_size;

/* Constants */
#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

void vm_swap_init(void) {
    ASSERT(SECTORS_PER_PAGE > 0);
    swap_block = block_get_role(BLOCK_SWAP);
    if (swap_block == NULL) {
        PANIC("Error: Can't initialize swap block");
        NOT_REACHED();
    }

    swap_size = block_size(swap_block);
    swap_available = bitmap_create(swap_size / SECTORS_PER_PAGE);
    bitmap_set_all(swap_available, true);
}

void vm_swap_in(swap_index_t swap_index, void *page) {
    ASSERT(is_user_vaddr(page));
    ASSERT(bitmap_test(swap_available, swap_index) == false); // false: 이미 할당된 슬롯이어야 함

    int i;
    for (i = 0; i < SECTORS_PER_PAGE; i++) {
        block_read(swap_block,
                   swap_index * SECTORS_PER_PAGE + i,
                   page + i * BLOCK_SECTOR_SIZE);
    }

    bitmap_set(swap_available, swap_index, true); // 다시 available로
}

swap_index_t vm_swap_out(void *page) {
    ASSERT(page >= PHYS_BASE);  // 유저 영역 검증
    swap_index_t index = bitmap_scan(swap_available, 0, 1, true);
    if (index == BITMAP_ERROR) PANIC("No available swap slot!");

    int i;
    for (i = 0; i < SECTORS_PER_PAGE; i++) {
        block_write(swap_block,
                    index * SECTORS_PER_PAGE + i,
                    page + i * BLOCK_SECTOR_SIZE);
    }

    bitmap_set(swap_available, index, false);  // false: 사용 중
    return index;
}

void vm_swap_free(swap_index_t swap_index) {
  // 1. 유효 범위 검사
  ASSERT(swap_index < swap_size);

  // 2. 이미 비어있는 슬롯인지 확인
  ASSERT(!bitmap_test(swap_available, swap_index));  // false여야 사용 중이라는 뜻

  // 3. 해당 슬롯을 다시 'available' 상태로 설정
  bitmap_set(swap_available, swap_index, true);
}