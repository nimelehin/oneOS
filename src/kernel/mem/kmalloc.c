/*
 * Copyright (C) 2020 Nikita Melekhin
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 */

#include <algo/bitmap.h>
#include <drivers/display.h>
#include <mem/kmalloc.h>
#include <mem/vmm/zoner.h>

struct kmalloc_header {
    uint32_t len;
};
typedef struct kmalloc_header kmalloc_header_t;

static zone_t _kmalloc_zone;
static uint32_t _kmalloc_bitmap_len = 0;
static uint8_t* _kmalloc_bitmap;
static bitmap_t bitmap;

static inline uint32_t kmalloc_to_vaddr(int start)
{
    uint32_t vaddr = (uint32_t)_kmalloc_zone.start + start * KMALLOC_BLOCK_SIZE;
    return (uint32_t)_kmalloc_zone.start + start * KMALLOC_BLOCK_SIZE;
}

static inline int kmalloc_to_index(uint32_t vaddr)
{
    return (vaddr - (uint32_t)_kmalloc_zone.start) / KMALLOC_BLOCK_SIZE;
}

static void _kmalloc_init_bitmap()
{
    _kmalloc_bitmap = (uint8_t*)_kmalloc_zone.start;
    _kmalloc_bitmap_len = (KMALLOC_SPACE_SIZE / KMALLOC_BLOCK_SIZE / 8);

    bitmap = bitmap_wrap(_kmalloc_bitmap, _kmalloc_bitmap_len);
    memset(_kmalloc_bitmap, 0, _kmalloc_bitmap_len);

    /* Setting bitmap as a busy region. */
    int blocks_needed = (_kmalloc_bitmap_len + KMALLOC_BLOCK_SIZE - 1) / KMALLOC_BLOCK_SIZE;
    bitmap_set_range(bitmap, kmalloc_to_index((uint32_t)_kmalloc_bitmap), blocks_needed);
}

void kmalloc_init()
{
    _kmalloc_zone = zoner_new_zone(KMALLOC_SPACE_SIZE);
    _kmalloc_init_bitmap();
}

void* kmalloc(uint32_t size)
{
    int act_size = size + sizeof(kmalloc_header_t);

    int blocks_needed = (act_size + KMALLOC_BLOCK_SIZE - 1) / KMALLOC_BLOCK_SIZE;

    int start = bitmap_find_space(bitmap, blocks_needed);
    if (start < 0) {
        return 0;
    }

    kmalloc_header_t* space = (kmalloc_header_t*)kmalloc_to_vaddr(start);
    space->len = act_size;
    bitmap_set_range(bitmap, start, blocks_needed);

    return (void*)&space[1];
}

void* kmalloc_aligned(uint32_t size, uint32_t alignment)
{
    void* ptr = kmalloc(size + alignment + sizeof(void*));
    uint32_t max_addr = (uint32_t)ptr + alignment;
    void* aligned_ptr = (void*)(max_addr - (max_addr % alignment));
    ((void**)aligned_ptr)[-1] = ptr;
    return aligned_ptr;
}

void* kmalloc_page_aligned()
{
    return kmalloc_aligned(VMM_PAGE_SIZE, VMM_PAGE_SIZE);
}

void kfree(void* ptr)
{
    kmalloc_header_t* sptr = (kmalloc_header_t*)ptr;
    int blocks_to_delete = (sptr[-1].len + KMALLOC_BLOCK_SIZE - 1) / KMALLOC_BLOCK_SIZE;
    bitmap_unset_range(bitmap, kmalloc_to_index((uint32_t)&sptr[-1]), blocks_to_delete);
}

void kfree_aligned(void* ptr)
{
    kfree(((void**)ptr)[-1]);
}

void* krealloc(void* ptr, uint32_t new_size)
{
    uint32_t old_size = ((kmalloc_header_t*)ptr)[-1].len;
    if (old_size == new_size) {
        return ptr;
    }

    uint8_t* new_area = kmalloc(new_size);
    if (new_area == 0) {
        return 0;
    }

    memcpy(new_area, ptr, new_size > old_size ? new_size : old_size);
    kfree(ptr);

    return new_area;
}