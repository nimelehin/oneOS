/*
 * Copyright (C) 2020 Nikita Melekhin
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 */

#include <mem/vmm/pde.h>

void table_desc_set_attrs(table_desc_t* pde, uint32_t attrs)
{
    *pde |= attrs;
}

void table_desc_del_attrs(table_desc_t* pde, uint32_t attrs)
{
    *pde &= ~(attrs);
}

bool table_desc_has_attrs(table_desc_t pde, uint32_t attrs)
{
    return ((pde & attrs) > 0);
}

void table_desc_set_frame(table_desc_t* pde, uint32_t frame)
{
    table_desc_del_frame(pde);
    *pde |= (frame << TABLE_DESC_FRAME_OFFSET);
}

void table_desc_del_frame(table_desc_t* pde)
{
    *pde &= ((1 << (TABLE_DESC_FRAME_OFFSET)) - 1);
}

bool table_desc_is_present(table_desc_t pde)
{
    return ((pde & TABLE_DESC_PRESENT) > 0);
}

bool table_desc_is_writable(table_desc_t pde)
{
    return ((pde & TABLE_DESC_WRITABLE) > 0);
}

bool table_desc_is_4mb(table_desc_t pde)
{
    return ((pde & TABLE_DESC_4MB) > 0);
}

bool table_desc_is_copy_on_write(table_desc_t pde)
{
    return ((pde & TABLE_DESC_COPY_ON_WRITE) > 0);
}

uint32_t table_desc_get_frame(table_desc_t pde)
{
    return ((pde >> TABLE_DESC_FRAME_OFFSET) << TABLE_DESC_FRAME_OFFSET);
}
