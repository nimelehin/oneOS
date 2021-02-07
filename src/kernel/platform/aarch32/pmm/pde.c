/*
 * Copyright (C) 2020 Nikita Melekhin
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 */

#include <platform/aarch32/vmm/pde.h>

void table_desc_init(table_desc_t* pde)
{
    pde->data = 0;
    pde->valid = 0;
    pde->zero1 = 0;
    pde->zero2 = 0;
    pde->zero3 = 0;
    pde->imp = 0;
    pde->domain = 0b0011;
}

void table_desc_clear(table_desc_t* pde)
{
    pde->data = 0;
}

void table_desc_set_attrs(table_desc_t* pde, uint32_t attrs)
{
    if ((attrs & TABLE_DESC_PRESENT) == TABLE_DESC_PRESENT) {
        pde->valid = 1;
    }
    if ((attrs & TABLE_DESC_COPY_ON_WRITE) == TABLE_DESC_COPY_ON_WRITE) {
        pde->imp = 1;
    }
}

void table_desc_del_attrs(table_desc_t* pde, uint32_t attrs)
{
    if ((attrs & TABLE_DESC_PRESENT) == TABLE_DESC_PRESENT) {
        pde->valid = 0;
    }
    if ((attrs & TABLE_DESC_COPY_ON_WRITE) == TABLE_DESC_COPY_ON_WRITE) {
        pde->imp = 0;
    }
}

bool table_desc_has_attrs(table_desc_t pde, uint32_t attrs)
{
    if ((attrs & TABLE_DESC_PRESENT) == TABLE_DESC_PRESENT) {
        if (pde.valid == 0) {
            return false;
        }
    }
    if ((attrs & TABLE_DESC_COPY_ON_WRITE) == TABLE_DESC_COPY_ON_WRITE) {
        if (pde.imp == 0) {
            return false;
        }
    }
    return true;
}

void table_desc_set_frame(table_desc_t* pde, uint32_t paddr)
{
    table_desc_del_frame(pde);
    pde->baddr = (paddr >> TABLE_DESC_FRAME_OFFSET);
}

void table_desc_del_frame(table_desc_t* pde)
{
    pde->baddr = 0;
}

bool table_desc_is_present(table_desc_t pde)
{
    return pde.valid;
}

bool table_desc_is_writable(table_desc_t pde)
{
    return 1;
}

bool table_desc_is_4mb(table_desc_t pde)
{
    return 0;
}

bool table_desc_is_copy_on_write(table_desc_t pde)
{
    return pde.imp;
}

uint32_t table_desc_get_frame(table_desc_t pde)
{
    return ((pde.baddr) << TABLE_DESC_FRAME_OFFSET);
}
