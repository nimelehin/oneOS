/*
 * Copyright (C) 2020 Nikita Melekhin
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 */

#include <drivers/display.h>
#include <errno.h>
#include <global.h>
#include <log.h>
#include <mem/kmalloc.h>
#include <mem/vmm/vmm.h>
#include <mem/vmm/zoner.h>
#include <tasking/tasking.h>
#include <utils/kassert.h>
#include <x86/common.h>

// #define VMM_DEBUG

#define pdir_t pdirectory_t
#define VMM_OFFSET_IN_DIRECTORY(a) (((a) >> 22) & 0x3ff)
#define VMM_OFFSET_IN_TABLE(a) (((a) >> 12) & 0x3ff)
#define VMM_OFFSET_IN_PAGE(a) ((a)&0xfff)
#define FRAME(addr) (addr / VMM_PAGE_SIZE)
#define PAGE_START(vaddr) ((vaddr >> 12) << 12)

#define VMM_KERNEL_TABLES_START 768
#define VMM_USER_TABLES_START 0
#define IS_INDIVIDUAL_PER_DIR(index) (index < VMM_KERNEL_TABLES_START || (index == VMM_OFFSET_IN_DIRECTORY(pspace_zone.start)))

static pdir_t* _vmm_kernel_pdir;
static pdir_t* _vmm_active_pdir;
static zone_t pspace_zone;
static uint32_t kernel_ptables_start_paddr;

#define vmm_kernel_pdir_phys2virt(paddr) ((void*)((uint32_t)paddr + KERNEL_BASE - KERNEL_PM_BASE))

/**
 * PRIVATE FUNCTIONS
 */

inline static void* _vmm_pspace_get_vaddr_of_active_pdir();
inline static void* _vmm_pspace_get_nth_active_ptable(uint32_t n);
inline static void* _vmm_pspace_get_vaddr_of_active_ptable(uint32_t vaddr);
static bool _vmm_split_pspace();
static void _vmm_pspace_init();
static table_desc_t _vmm_pspace_gen();
static void* _vmm_kernel_convert_vaddr2paddr(uint32_t vaddr);
static void* _vmm_convert_vaddr2paddr(uint32_t vaddr);

inline static void* _vmm_alloc_kernel_block();
static bool _vmm_init_switch_to_kernel_pdir();
static void _vmm_map_init_kernel_pages(uint32_t paddr, uint32_t vaddr);
static bool _vmm_create_kernel_ptables();
static bool _vmm_map_kernel();

inline static uint32_t _vmm_round_ceil_to_page(uint32_t value);
inline static uint32_t _vmm_round_floor_to_page(uint32_t value);

static bool _vmm_is_copy_on_write(uint32_t vaddr);
static int _vmm_resolve_copy_on_write(uint32_t vaddr);
static void _vmm_ensure_cow_for_page(uint32_t vaddr);
static void _vmm_ensure_cow_for_range(uint32_t vaddr, uint32_t length);
static int _vmm_copy_page_to_resolve_cow(uint32_t vaddr, ptable_t* src_ptable);

static bool _vmm_is_zeroing_on_demand(uint32_t vaddr);
static void _vmm_resolve_zeroing_on_demand(uint32_t vaddr);

inline static void _vmm_flush_tlb();
inline static void _vmm_flush_tlb_entry(uint32_t vaddr);
inline static void _vmm_enable_write_protect();
inline static void _vmm_disable_write_protect();

static int _vmm_self_test();

/**
 * PSPACE FUNCTIONS
 * 
 * Pspace is a space with all current tables mapped there.
 * The space starts from @pspace_zone.start and length is 4mb
 */

/**
 * The function is supposed to setup pspace_zone.start
 * Every virtual space has its own user's ptables in the area.
 * The kernel's patables are the same.
 */

inline static void* _vmm_pspace_get_vaddr_of_active_pdir()
{
    return (void*)_vmm_active_pdir;
}

inline static void* _vmm_pspace_get_nth_active_ptable(uint32_t n)
{
    return (void*)(pspace_zone.start + n * VMM_PAGE_SIZE);
}

inline static void* _vmm_pspace_get_vaddr_of_active_ptable(uint32_t vaddr)
{
    return (void*)_vmm_pspace_get_nth_active_ptable(VMM_OFFSET_IN_DIRECTORY(vaddr));
}

static bool _vmm_split_pspace()
{
    pspace_zone = zoner_new_zone(4 * MB);

    if (VMM_OFFSET_IN_TABLE(pspace_zone.start) != 0) {
        kpanic("WRONG PSPACE START ADDR");
    }

    // FIXME: remove constant basing on kernel len.
    kernel_ptables_start_paddr = 0x220000;
    while ((uint32_t)pmm_alloc_block() < (kernel_ptables_start_paddr - 2 * VMM_PAGE_SIZE)) {
    }
    _vmm_kernel_pdir = (pdir_t*)_vmm_alloc_kernel_block();
    _vmm_active_pdir = (pdir_t*)_vmm_kernel_pdir;
    memset((void*)_vmm_active_pdir, 0, sizeof(*_vmm_active_pdir)); // TODO problem for now
}

/**
 * The function is used to init pspace
 * Used only in the first stage of VM init
 */
static void _vmm_pspace_init()
{
    uint32_t kernel_ptabels_vaddr = pspace_zone.start + VMM_KERNEL_TABLES_START * VMM_PAGE_SIZE; // map what
    uint32_t kernel_ptabels_paddr = kernel_ptables_start_paddr; // map to
    for (int i = VMM_KERNEL_TABLES_START; i < 1024; i++) {
        table_desc_t* ptable_desc = vmm_pdirectory_lookup(_vmm_active_pdir, kernel_ptabels_vaddr);
        if (!table_desc_is_present(*ptable_desc)) {
            // must present
            kpanic("PSPACE_6335 : BUG\n");
        }
        ptable_t* ptable_vaddr = (ptable_t*)(kernel_ptables_start_paddr + (VMM_OFFSET_IN_DIRECTORY(kernel_ptabels_vaddr) - VMM_KERNEL_TABLES_START) * VMM_PAGE_SIZE);
        page_desc_t* page = vmm_ptable_lookup(ptable_vaddr, kernel_ptabels_vaddr);
        page_desc_set_attrs(page, PAGE_DESC_PRESENT | PAGE_DESC_WRITABLE);
        page_desc_set_frame(page, FRAME(kernel_ptabels_paddr));
        kernel_ptabels_vaddr += VMM_PAGE_SIZE;
        kernel_ptabels_paddr += VMM_PAGE_SIZE;
    }
}

/**
 * The function is used to generate new pspace.
 * The function returns the table of itself. 
 */
static table_desc_t _vmm_pspace_gen()
{
    ptable_t* cur_ptable = (ptable_t*)_vmm_pspace_get_nth_active_ptable(VMM_OFFSET_IN_DIRECTORY(pspace_zone.start));

    uint32_t ptable_paddr = (uint32_t)pmm_alloc_block();
    zone_t tmp_zone = zoner_new_zone(VMM_PAGE_SIZE);
    ptable_t* new_ptable = (ptable_t*)tmp_zone.start;

    vmm_map_page((uint32_t)new_ptable, ptable_paddr, PAGE_READABLE | PAGE_WRITABLE | PAGE_EXECUTABLE);

    for (int i = 0; i < 1024; i++) {
        // coping all pages
        new_ptable->entities[i] = cur_ptable->entities[i];
    }

    page_desc_t pspace_page = 0;
    page_desc_set_attrs(&pspace_page, PAGE_DESC_PRESENT | PAGE_DESC_WRITABLE);
    page_desc_set_frame(&pspace_page, FRAME(ptable_paddr));
    new_ptable->entities[VMM_OFFSET_IN_DIRECTORY(pspace_zone.start)] = pspace_page;

    table_desc_t pspace_table = 0;
    table_desc_set_attrs(&pspace_table, TABLE_DESC_PRESENT | TABLE_DESC_WRITABLE);
    table_desc_set_frame(&pspace_table, FRAME(ptable_paddr));

    vmm_unmap_page((uint32_t)new_ptable);
    zoner_free_zone(tmp_zone);

    return pspace_table;
}

static void _vmm_free_pspace(pdirectory_t* pdir)
{
    table_desc_t* ptable_desc = &pdir->entities[VMM_OFFSET_IN_DIRECTORY(pspace_zone.start)];
    if (!table_desc_has_attrs(*ptable_desc, TABLE_DESC_PRESENT)) {
        return;
    }
    pmm_free_block((void*)table_desc_get_frame(*ptable_desc));
    table_desc_del_frame(ptable_desc);
}

/**
 * The function is used to traslate a virtual address into physical
 * Used only in the first stage of VM init
 */
static void* _vmm_kernel_convert_vaddr2paddr(uint32_t vaddr)
{
    ptable_t* ptable_vaddr = (ptable_t*)(kernel_ptables_start_paddr + (VMM_OFFSET_IN_DIRECTORY(vaddr) - VMM_KERNEL_TABLES_START) * VMM_PAGE_SIZE);
    page_desc_t* page_desc = vmm_ptable_lookup(ptable_vaddr, vaddr);
    return (void*)((page_desc_get_frame(*page_desc)) | (vaddr & 0xfff));
}

/**
 * The function is used to traslate a virtual address into physical
 */
static void* _vmm_convert_vaddr2paddr(uint32_t vaddr)
{
    ptable_t* ptable_vaddr = (ptable_t*)_vmm_pspace_get_vaddr_of_active_ptable(vaddr);
    page_desc_t* page_desc = vmm_ptable_lookup(ptable_vaddr, vaddr);
    return (void*)((page_desc_get_frame(*page_desc)) | (vaddr & 0xfff));
}

/**
 * VM INITIALIZATION FUNCTIONS
 */

inline static void* _vmm_alloc_kernel_block()
{
    return vmm_kernel_pdir_phys2virt(pmm_alloc_block());
}

/**
 * The function is used to update active pdir
 * Used only in the first stage of VM init
 */
static bool _vmm_init_switch_to_kernel_pdir()
{
    _vmm_active_pdir = _vmm_kernel_pdir;
    cli();
    /* Should have :Memory here? */
    asm volatile("mov %%eax, %%cr3"
                 :
                 : "a"((uint32_t)_vmm_kernel_convert_vaddr2paddr((uint32_t)_vmm_active_pdir)));
    sti();
    return true;
}

/**
 * The function is used to map kernel pages
 * Used only in the first stage of VM init
 */
static void _vmm_map_init_kernel_pages(uint32_t paddr, uint32_t vaddr)
{
    ptable_t* ptable_paddr = (ptable_t*)(kernel_ptables_start_paddr + (VMM_OFFSET_IN_DIRECTORY(vaddr) - VMM_KERNEL_TABLES_START) * VMM_PAGE_SIZE);
    for (uint32_t phyz = paddr, virt = vaddr, i = 0; i < 1024; phyz += VMM_PAGE_SIZE, virt += VMM_PAGE_SIZE, i++) {
        pte_t new_page = (FRAME(phyz) << 12) | 3;
        ptable_paddr->entities[i] = new_page;
    }
}

/**
 * The function is supposed to create all kernel tables and map necessary
 * data into _vmm_kernel_pdir.
 * Used only in the first stage of VM init
 */
static bool _vmm_create_kernel_ptables()
{
    uint32_t table_coverage = VMM_PAGE_SIZE * 1024;
    uint32_t kernel_ptabels_vaddr = VMM_KERNEL_TABLES_START * table_coverage;

    for (int i = VMM_KERNEL_TABLES_START; i < 1024; i++, kernel_ptabels_vaddr += table_coverage) {
        // updating table descriptor of kernel's pdir
        table_desc_t* ptable_desc = vmm_pdirectory_lookup(_vmm_kernel_pdir, kernel_ptabels_vaddr);
        uint32_t paddr = (uint32_t)pmm_alloc_block();
        if (!paddr) {
            kpanic("PADDR_5546 : BUG\n");
        }
        table_desc_set_attrs(ptable_desc, TABLE_DESC_PRESENT | TABLE_DESC_WRITABLE);

        /**
         * VMM_OFFSET_IN_DIRECTORY(pspace_zone.start) shows number of table where pspace starts. 
         * Since pspace is right after kernel, let's protect them and not give user access to the whole
         * ptable (and since ptable is not user, all pages inside it are not user too).
         */
        if (i > VMM_OFFSET_IN_DIRECTORY(pspace_zone.start)) {
            table_desc_set_attrs(ptable_desc, TABLE_DESC_USER);
        }

        table_desc_set_frame(ptable_desc, FRAME(paddr));
    }

    _vmm_map_init_kernel_pages(0x00100000, 0xc0000000);
    _vmm_map_init_kernel_pages(0x00000000, 0xffc00000);

    return true;
}

/**
 * The function is supposed to map secondary data for kernel's pdir.
 * Used only in the first stage of VM init
 */
static bool _vmm_map_kernel()
{
    vmm_map_pages(0x00000000, 0x00000000, 1024, PAGE_READABLE | PAGE_WRITABLE | PAGE_EXECUTABLE);
    return true;
}

int vmm_setup()
{
    zoner_init(0xc0400000);
    _vmm_split_pspace();
    _vmm_create_kernel_ptables();
    _vmm_pspace_init();
    _vmm_init_switch_to_kernel_pdir();
    _vmm_map_kernel();
    zoner_place_bitmap();
    kmalloc_init();
    if (_vmm_self_test() < 0) {
        kpanic("VMM SELF TEST Not passed");
    }
    return 0;
}

/**
 * VM TOOLS
 */

inline static uint32_t _vmm_round_ceil_to_page(uint32_t value)
{
    if ((value & (VMM_PAGE_SIZE - 1)) != 0) {
        value += VMM_PAGE_SIZE;
        value &= (0xffffffff - (VMM_PAGE_SIZE - 1));
    }
    return value;
}

inline static uint32_t _vmm_round_floor_to_page(uint32_t value)
{
    return (value & (0xffffffff - (VMM_PAGE_SIZE - 1)));
}

table_desc_t* vmm_pdirectory_lookup(pdirectory_t* pdir, uint32_t vaddr)
{
    if (pdir) {
        return &pdir->entities[VMM_OFFSET_IN_DIRECTORY(vaddr)];
    }
    return 0;
}

page_desc_t* vmm_ptable_lookup(ptable_t* ptable, uint32_t vaddr)
{
    if (ptable) {
        return &ptable->entities[VMM_OFFSET_IN_TABLE(vaddr)];
    }
    return 0;
}

/**
 * The function is supposed to create new ptable (not kernel) and
 * rebuild pspace to match to the new setup
 */
int vmm_allocate_ptable(uint32_t vaddr)
{
    if (_vmm_active_pdir == 0) {
        return -VMM_ERR_PDIR;
    }

    uint32_t ptable_paddr = (uint32_t)pmm_alloc_block();
    if (!ptable_paddr) {
        log_error(" vmm_allocate_ptable: No free space in pmm");
        return -VMM_ERR_NO_SPACE;
    }
    uint32_t ptable_vaddr = (uint32_t)_vmm_pspace_get_vaddr_of_active_ptable(vaddr);

    table_desc_t* ptable_desc = vmm_pdirectory_lookup(_vmm_active_pdir, vaddr);
    table_desc_clear(ptable_desc);
    table_desc_set_attrs(ptable_desc, TABLE_DESC_PRESENT | TABLE_DESC_WRITABLE | TABLE_DESC_USER);
    table_desc_set_frame(ptable_desc, FRAME(ptable_paddr));

    int ret = vmm_map_page(ptable_vaddr, ptable_paddr, PAGE_READABLE | PAGE_WRITABLE | PAGE_EXECUTABLE | PAGE_CHOOSE_OWNER(vaddr));
    if (ret == 0) {
        memset((void*)ptable_vaddr, 0, VMM_PAGE_SIZE);
    }
    return ret;
}

static int vmm_free_ptable(uint32_t ptable_indx)
{
    if (_vmm_active_pdir == 0) {
        return -VMM_ERR_PDIR;
    }

    table_desc_t* ptable_desc = &_vmm_active_pdir->entities[ptable_indx];

    if (!table_desc_has_attrs(*ptable_desc, TABLE_DESC_PRESENT)) {
        return -EFAULT;
    }

    if (table_desc_has_attrs(*ptable_desc, TABLE_DESC_COPY_ON_WRITE)) {
        return -EFAULT;
    }

    pmm_free_block((void*)table_desc_get_frame(*ptable_desc));
    table_desc_del_frame(ptable_desc);

    return 0;
}

/**
 * The function is supposed to map vaddr to paddr
 */

int vmm_map_page(uint32_t vaddr, uint32_t paddr, uint32_t settings)
{
    if (!_vmm_active_pdir) {
        return -VMM_ERR_PDIR;
    }

    bool is_writable = ((settings & PAGE_WRITABLE) > 0);
    bool is_readable = ((settings & PAGE_READABLE) > 0);
    bool is_executable = ((settings & PAGE_EXECUTABLE) > 0);
    bool is_not_cacheable = ((settings & PAGE_NOT_CACHEABLE) > 0);
    bool is_cow = ((settings & PAGE_COW) > 0);
    bool is_user = ((settings & PAGE_USER) > 0);

    table_desc_t* ptable_desc = vmm_pdirectory_lookup(_vmm_active_pdir, vaddr);
    if (!table_desc_is_present(*ptable_desc)) {
        vmm_allocate_ptable(vaddr);
    }

    ptable_t* ptable = (ptable_t*)_vmm_pspace_get_vaddr_of_active_ptable(vaddr);
    page_desc_t* page = vmm_ptable_lookup(ptable, vaddr);
    page_desc_set_attrs(page, PAGE_DESC_PRESENT);
    page_desc_set_frame(page, FRAME(paddr));

    if (is_writable) {
        page_desc_set_attrs(page, PAGE_DESC_WRITABLE);
    }

    if (is_user) {
        page_desc_set_attrs(page, PAGE_DESC_USER);
    }

    if (is_not_cacheable) {
        page_desc_set_attrs(page, PAGE_DESC_NOT_CACHEABLE);
    }

#ifdef VMM_DEBUG
    log("Page mapped %x in pdir: %x", vaddr, vmm_get_active_pdir());
#endif

    _vmm_flush_tlb_entry(vaddr);

    return 0;
}

int vmm_unmap_page(uint32_t vaddr)
{
    if (!_vmm_active_pdir) {
        return -VMM_ERR_PDIR;
    }

    table_desc_t* ptable_desc = vmm_pdirectory_lookup(_vmm_active_pdir, vaddr);
    if (!table_desc_is_present(*ptable_desc)) {
        return -VMM_ERR_PTABLE;
    }

    ptable_t* ptable = (ptable_t*)_vmm_pspace_get_vaddr_of_active_ptable(vaddr);
    page_desc_t* page = vmm_ptable_lookup(ptable, vaddr);
    page_desc_del_attrs(page, PAGE_DESC_PRESENT);
    page_desc_del_attrs(page, PAGE_DESC_WRITABLE);
    page_desc_del_frame(page);
    _vmm_flush_tlb_entry(vaddr);

    return 0;
}

/**
 * The function is supposed to map a sequence of vaddrs to paddrs
 */
int vmm_map_pages(uint32_t vaddr, uint32_t paddr, uint32_t n_pages, uint32_t settings)
{
    if ((paddr & 0xfff) || (vaddr & 0xfff)) {
        return -VMM_ERR_BAD_ADDR;
    }

    int status = 0;
    for (; n_pages; paddr += VMM_PAGE_SIZE, vaddr += VMM_PAGE_SIZE, n_pages--) {
        if (status = vmm_map_page(vaddr, paddr, settings) < 0) {
            return status;
        }
    }

    return 0;
}

/**
 * COPY ON WRITE FUNCTIONS
 */

static bool _vmm_is_copy_on_write(uint32_t vaddr)
{
    table_desc_t* ptable_desc = vmm_pdirectory_lookup(_vmm_active_pdir, vaddr);
    return table_desc_is_copy_on_write(*ptable_desc);
}

// TODO handle delete of tables and dirs
static int _vmm_resolve_copy_on_write(uint32_t vaddr)
{
    table_desc_t* ptable_desc = vmm_pdirectory_lookup(_vmm_active_pdir, vaddr);

    /* copying old ptable to kernel */
    ptable_t* src_ptable = kmalloc_page_aligned();
    ptable_t* root_ptable = _vmm_pspace_get_vaddr_of_active_ptable(vaddr);
    memcpy((uint8_t*)src_ptable, (uint8_t*)root_ptable, VMM_PAGE_SIZE);

    /* setting up new ptable */
    int res = vmm_allocate_ptable(vaddr);
    if (res != 0) {
        return res;
    }

    ptable_t* new_ptable = _vmm_pspace_get_vaddr_of_active_ptable(vaddr);

    /* FIXME: Currently we do that for all pages in the table. */
    for (int i = 0; i < 1024; i++) {
        uint32_t page_vaddr = ((vaddr >> 22) << 22) + (i * VMM_PAGE_SIZE);
        page_desc_t* page_desc = vmm_ptable_lookup(src_ptable, page_vaddr);
        if ((uint32_t)*page_desc != 0) {
            _vmm_copy_page_to_resolve_cow(page_vaddr, src_ptable);
        }
    }

    kfree_aligned(src_ptable);
}

static void _vmm_ensure_cow_for_page(uint32_t vaddr)
{
    if (_vmm_is_copy_on_write(vaddr)) {
        _vmm_resolve_copy_on_write(vaddr);
    }
}

static void _vmm_ensure_cow_for_range(uint32_t vaddr, uint32_t length)
{
    uint32_t page_addr = PAGE_START(vaddr);
    while (page_addr < vaddr + length) {
        _vmm_ensure_cow_for_page(page_addr);
        page_addr += VMM_PAGE_SIZE;
    }
}

/**
 * The function is supposed to copy a page from @src_ptable to active
 * ptable. The page which is copied has address @vaddr. ONLY TO RESOLVE COW!
 */
static int _vmm_copy_page_to_resolve_cow(uint32_t vaddr, ptable_t* src_ptable)
{
    page_desc_t* old_page_desc = vmm_ptable_lookup(src_ptable, vaddr);

    /* Based on an old page */
    vmm_load_page(vaddr, page_desc_get_settings_ignore_cow(*old_page_desc));

    /* Mapping the old page to do a copy */
    zone_t tmp_zone = zoner_new_zone(VMM_PAGE_SIZE);
    uint32_t old_page_vaddr = (uint32_t)tmp_zone.start;
    uint32_t old_page_paddr = page_desc_get_frame(*old_page_desc);
    vmm_map_page(old_page_vaddr, old_page_paddr, PAGE_READABLE | PAGE_WRITABLE | PAGE_EXECUTABLE);

    /*  We don't need to check if there is a problem with cow, since this is a special copy function
        to resolve cow. So, we know that pages were created recently. Checking cow here would cause an
        ub, since table has not been setup correctly yet. */
    memcpy((uint8_t*)vaddr, (uint8_t*)old_page_vaddr, VMM_PAGE_SIZE);

    /* Freeing */
    vmm_unmap_page(old_page_vaddr);
    zoner_free_zone(tmp_zone);
    return 0;
}

/**
 * ZEROING ON DEMAND FUNCTIONS
 */

static bool _vmm_is_zeroing_on_demand(uint32_t vaddr)
{
    table_desc_t* ptable_desc = vmm_pdirectory_lookup(_vmm_active_pdir, vaddr);
    if (table_desc_has_attrs(*ptable_desc, TABLE_DESC_ZEROING_ON_DEMAND)) {
        return true;
    }
    ptable_t* ptable = _vmm_pspace_get_vaddr_of_active_ptable(vaddr);
    table_desc_t* ppage_desc = vmm_ptable_lookup(ptable, vaddr);
    return page_desc_has_attrs(*ppage_desc, PAGE_DESC_ZEROING_ON_DEMAND);
}

static void _vmm_resolve_zeroing_on_demand(uint32_t vaddr)
{
    table_desc_t* ptable_desc = vmm_pdirectory_lookup(_vmm_active_pdir, vaddr);
    ptable_t* ptable = _vmm_pspace_get_vaddr_of_active_ptable(vaddr);

    if (table_desc_has_attrs(*ptable_desc, TABLE_DESC_ZEROING_ON_DEMAND)) {
        for (int i = 0; i < 1024; i++) {
            page_desc_set_attrs(&ptable->entities[i], PAGE_DESC_ZEROING_ON_DEMAND);
            page_desc_del_attrs(&ptable->entities[i], PAGE_DESC_WRITABLE);
        }
        table_desc_del_attrs(ptable_desc, TABLE_DESC_ZEROING_ON_DEMAND);
    }

    table_desc_t* ppage_desc = vmm_ptable_lookup(ptable, vaddr);

    uint8_t* dest = (uint8_t*)_vmm_round_floor_to_page(vaddr);
    memset(dest, 0, VMM_PAGE_SIZE);

    page_desc_del_attrs(ppage_desc, PAGE_DESC_ZEROING_ON_DEMAND);
    page_desc_set_attrs(ppage_desc, PAGE_DESC_WRITABLE);
}

/**
 * USER PDIR FUNCTIONS
 */

/**
 * The function is supposed to create all new user's pdir.
 */
pdirectory_t* vmm_new_user_pdir()
{
    pdirectory_t* pdir = (pdirectory_t*)kmalloc_page_aligned();

    for (int i = VMM_USER_TABLES_START; i < VMM_KERNEL_TABLES_START; i++) {
        pdir->entities[i] = 0;
    }

    for (int i = VMM_KERNEL_TABLES_START; i < 1024; i++) {
        if (!IS_INDIVIDUAL_PER_DIR(i)) {
            pdir->entities[i] = _vmm_kernel_pdir->entities[i];
        }
    }

    pdir->entities[VMM_OFFSET_IN_DIRECTORY(pspace_zone.start)] = _vmm_pspace_gen();
    return pdir;
}

/**
 * The function created a new user's pdir from the active pdir.
 * After copying the task we need to flush tlb. To do that we have to
 * to reload cr3 tlb entries. 
 */
pdirectory_t* vmm_new_forked_user_pdir()
{
    pdirectory_t* new_pdir = (pdirectory_t*)kmalloc_page_aligned();

    // coping all tables
    for (int i = 0; i < 1024; i++) {
        new_pdir->entities[i] = _vmm_active_pdir->entities[i];
    }
    new_pdir->entities[VMM_OFFSET_IN_DIRECTORY(pspace_zone.start)] = _vmm_pspace_gen();

    for (int i = 0; i < VMM_KERNEL_TABLES_START; i++) {
        table_desc_t* act_ptable_desc = &_vmm_active_pdir->entities[i];
        if (table_desc_has_attrs(*act_ptable_desc, TABLE_DESC_PRESENT)) {
            // COW: blocking current pdir
            table_desc_del_attrs(act_ptable_desc, TABLE_DESC_WRITABLE);
            table_desc_set_attrs(act_ptable_desc, TABLE_DESC_COPY_ON_WRITE);

            // COW: blocking new pdir
            table_desc_t* new_ptable_desc = &new_pdir->entities[i];
            table_desc_del_attrs(new_ptable_desc, TABLE_DESC_WRITABLE);
            table_desc_set_attrs(new_ptable_desc, TABLE_DESC_COPY_ON_WRITE);
        }
    }

    _vmm_flush_tlb();

    return new_pdir;
}

int vmm_free_pdir(pdirectory_t* pdir)
{
    if (!pdir) {
        return -EINVAL;
    }

    if (pdir == _vmm_kernel_pdir) {
        return -EACCES;
    }

    pdirectory_t* cur_pdir = vmm_get_active_pdir();
    vmm_switch_pdir(pdir);

    for (int i = 0; i < VMM_KERNEL_TABLES_START; i++) {
        vmm_free_ptable(i);
    }

    if (cur_pdir == pdir) {
        vmm_switch_pdir(_vmm_kernel_pdir);
    } else {
        vmm_switch_pdir(cur_pdir);
    }
    _vmm_free_pspace(pdir);
    kfree_aligned(pdir);
    return 0;
}

void* vmm_bring_to_kernel(uint8_t* src, uint32_t length)
{
    if ((uint32_t)src >= KERNEL_BASE) {
        return src;
    }
    uint8_t* kaddr = kmalloc(length);
    memcpy(kaddr, src, length);
    return (void*)kaddr;
}

void vmm_copy_to_pdir(pdirectory_t* pdir, uint8_t* src, uint32_t dest_vaddr, uint32_t length)
{
    pdirectory_t* prev_pdir = vmm_get_active_pdir();
    uint8_t* ksrc;
    if ((uint32_t)src < KERNEL_BASE) {
        /* Means that it's in user space, so it should be copied here. */
        ksrc = vmm_bring_to_kernel(src, length);
    } else {
        ksrc = src;
    }

    vmm_switch_pdir(pdir);

    _vmm_ensure_cow_for_range(dest_vaddr, length);
    uint8_t* dest = (uint8_t*)dest_vaddr;
    memcpy(dest, ksrc, length);

    if ((uint32_t)src < KERNEL_BASE) {
        kfree(ksrc);
    }

    vmm_switch_pdir(prev_pdir);
}

void vmm_zero_user_pages(pdirectory_t* pdir)
{
    for (int i = 0; i < VMM_KERNEL_TABLES_START; i++) {
        table_desc_t* ptable_desc = &pdir->entities[i];
        table_desc_del_attrs(ptable_desc, TABLE_DESC_WRITABLE);
        table_desc_set_attrs(ptable_desc, TABLE_DESC_ZEROING_ON_DEMAND);
    }
}

pdirectory_t* vmm_get_active_pdir()
{
    return _vmm_active_pdir;
}

pdirectory_t* vmm_get_kernel_pdir()
{
    return _vmm_kernel_pdir;
}

/**
 * PF HANDLER FUNCTIONS
 */

int vmm_tune_page(uint32_t vaddr, uint32_t settings)
{
    bool is_writable = ((settings & PAGE_WRITABLE) > 0);
    bool is_readable = ((settings & PAGE_READABLE) > 0);
    bool is_executable = ((settings & PAGE_EXECUTABLE) > 0);
    bool is_not_cacheable = ((settings & PAGE_NOT_CACHEABLE) > 0);
    bool is_cow = ((settings & PAGE_COW) > 0);
    bool is_user = ((settings & PAGE_USER) > 0);

    ptable_t* ptable = (ptable_t*)_vmm_pspace_get_vaddr_of_active_ptable(vaddr);
    page_desc_t* page = vmm_ptable_lookup(ptable, vaddr);

    if (page_desc_is_present(*page)) {
        is_user ? page_desc_set_attrs(page, PAGE_DESC_USER) : page_desc_del_attrs(page, PAGE_DESC_USER);
        is_writable ? page_desc_set_attrs(page, PAGE_DESC_WRITABLE) : page_desc_del_attrs(page, PAGE_DESC_WRITABLE);
        is_not_cacheable ? page_desc_set_attrs(page, PAGE_DESC_NOT_CACHEABLE) : page_desc_del_attrs(page, PAGE_DESC_NOT_CACHEABLE);
    } else {
        vmm_load_page(vaddr, settings);
    }

    _vmm_flush_tlb_entry(vaddr);
}

int vmm_load_page(uint32_t vaddr, uint32_t settings)
{
    uint32_t paddr = (uint32_t)pmm_alloc_block();
    if (!paddr) {
        /* TODO: Swap pages to make it able to allocate. */
        kpanic("NO PHYSICAL SPACE");
    }
    int res = vmm_map_page(vaddr, paddr, settings);
    uint8_t* dest = (uint8_t*)_vmm_round_floor_to_page(vaddr);
    memset(dest, 0, VMM_PAGE_SIZE);
    return res;
}

/**
 * The function is supposed to copy a page from @src_ptable to active
 * ptable. The page which is copied has address @vaddr. ONLY TO RESOLVE COW!
 */
int vmm_copy_page(uint32_t to_vaddr, uint32_t src_vaddr, ptable_t* src_ptable)
{
    page_desc_t* old_page_desc = vmm_ptable_lookup(src_ptable, src_vaddr);

    /* Based on an old page */
    ptable_t* cur_ptable = (ptable_t*)_vmm_pspace_get_vaddr_of_active_ptable(to_vaddr);
    page_desc_t* cur_page = vmm_ptable_lookup(cur_ptable, to_vaddr);

    if (!page_desc_is_present(*cur_page)) {
        vmm_load_page(to_vaddr, page_desc_get_settings_ignore_cow(*old_page_desc));
    } else {
        _vmm_ensure_cow_for_page(to_vaddr);
    }

    /* Mapping the old page to do a copy */
    zone_t tmp_zone = zoner_new_zone(VMM_PAGE_SIZE);
    uint32_t old_page_vaddr = (uint32_t)tmp_zone.start;
    uint32_t old_page_paddr = page_desc_get_frame(*old_page_desc);
    vmm_map_page(old_page_vaddr, old_page_paddr, PAGE_READABLE | PAGE_WRITABLE | PAGE_EXECUTABLE);

    memcpy((uint8_t*)to_vaddr, (uint8_t*)old_page_vaddr, VMM_PAGE_SIZE);

    /* Freeing */
    vmm_unmap_page(old_page_vaddr);
    zoner_free_zone(tmp_zone);
    return 0;
}

// currently unused and unoptimized with rewritten vmm
int vmm_alloc_page(page_desc_t* page)
{
    void* new_block = kmalloc_page_aligned();
    if (!new_block) {
        return -VMM_ERR_BAD_ADDR;
    }
    page_desc_set_attrs(page, PAGE_DESC_PRESENT);
    page_desc_set_frame(page, FRAME((uint32_t)new_block));
    return 0;
}

// currently unused and unoptimized with rewritten vmm
int vmm_free_page(page_desc_t* page)
{
    uint32_t frame = page_desc_get_frame(*page);
    pmm_free_block((void*)(frame * VMM_PAGE_SIZE));
    page_desc_del_attrs(page, PAGE_DESC_PRESENT);
    return 0;
}

int vmm_page_fault_handler(uint8_t info, uint32_t vaddr)
{
    if ((info & 0b11) == 0b11) {
        bool visited = false;
        if (_vmm_is_copy_on_write(vaddr)) {
            _vmm_resolve_copy_on_write(vaddr);
            visited = true;
        }
        if (_vmm_is_zeroing_on_demand(vaddr)) {
            _vmm_resolve_zeroing_on_demand(vaddr);
            visited = true;
        }
        if (!visited) {
            kprintf("\nWrite to not writable page");
            return SHOULD_CRASH;
        }
    } else {
        if ((info & 1) == 0) {
            if (PAGE_CHOOSE_OWNER(vaddr) == PAGE_USER && vmm_get_active_pdir() != vmm_get_kernel_pdir()) {
                proc_t* holder_proc = tasking_get_proc_by_pdir(vmm_get_active_pdir());
                if (!holder_proc) {
                    kpanic("No proc with the pdir\n");
                }

                proc_zone_t* zone = proc_find_zone(holder_proc, vaddr);
                if (!zone) {
                    return SHOULD_CRASH;
                }
#ifdef VMM_DEBUG
                log("Mmap page %x for %d pid: %x", vaddr, RUNNIG_THREAD->process->pid, zone->flags);
#endif
                vmm_load_page(vaddr, zone->flags);

                if (zone->type == ZONE_TYPE_MAPPED_FILE_PRIVATLY) {
                    uint32_t offset_for_this_page = zone->offset + (PAGE_START(vaddr) - zone->start);
                    vfs_read(zone->fd, (uint8_t*)PAGE_START(vaddr), VMM_PAGE_SIZE);
                }
            } else {
                /* FIXME: Now we have a standard zone for kernel, but it's better to do the same thing as for user's pages */
                vmm_load_page(vaddr, PAGE_READABLE | PAGE_WRITABLE | PAGE_EXECUTABLE);
            }
        } else {
            proc_zone_t* zone = proc_find_zone(RUNNIG_THREAD->process, vaddr);
            log_error("PF: info %d vaddr %x pdir %x pid %d", info, vaddr, vmm_get_active_pdir(), RUNNIG_THREAD->process->pid);
            return SHOULD_CRASH;
        }
    }

    return OK;
}

/**
 * CPU BASED FUNCTIONS
 */

inline static void _vmm_flush_tlb()
{
    asm volatile("mov %%eax, %%cr3"
                 :
                 : "a"((uint32_t)_vmm_convert_vaddr2paddr((uint32_t)_vmm_active_pdir)));
}

inline static void _vmm_flush_tlb_entry(uint32_t vaddr)
{
    asm volatile("invlpg (%0)" ::"r"(vaddr)
                 : "memory");
}

inline static void _vmm_enable_write_protect()
{
    asm volatile("mov %cr0, %eax");
    asm volatile("or $0x10000, %eax");
    asm volatile("mov %eax, %cr0");
}

inline static void _vmm_disable_write_protect()
{
    asm volatile("mov %cr0, %eax");
    asm volatile("and $0xFFFEFFFF, %eax");
    asm volatile("mov %eax, %cr0");
}

int vmm_switch_pdir(pdirectory_t* pdir)
{
    if (((uint32_t)pdir & (VMM_PAGE_SIZE - 1)) != 0) {
        kpanic("vmm_switch_pdir: wrong pdir");
    }

    cli();
    if (_vmm_active_pdir == pdir) {
        sti();
        return 0;
    }
    _vmm_active_pdir = pdir;
    asm volatile("mov %%eax, %%cr3"
                 :
                 : "a"((uint32_t)_vmm_convert_vaddr2paddr((uint32_t)pdir)));
    sti();
    return 0;
}

void vmm_enable_paging()
{
    asm volatile("mov %cr0, %eax");
    asm volatile("or $0x80000000, %eax");
    asm volatile("mov %eax, %cr0");
}

void vmm_disable_paging()
{
    asm volatile("mov %cr0, %eax");
    asm volatile("and $0x7FFFFFFF, %eax");
    asm volatile("mov %eax, %cr0");
}

/**
 * VM SELF TEST FUNCTIONS
 */

static int _vmm_self_test()
{
    vmm_map_pages(0x00f0000, 0x8f000000, 1, PAGE_READABLE | PAGE_WRITABLE | PAGE_EXECUTABLE);
    bool correct = true;
    correct &= ((uint32_t)_vmm_convert_vaddr2paddr(KERNEL_BASE) == KERNEL_PM_BASE);
    correct &= ((uint32_t)_vmm_convert_vaddr2paddr(0xffc00000) == 0x0);
    correct &= ((uint32_t)_vmm_convert_vaddr2paddr(0x100) == 0x100);
    correct &= ((uint32_t)_vmm_convert_vaddr2paddr(0x8f000000) == 0x00f0000);
    if (correct) {
        return 0;
    }
    return 1;
}

static bool vmm_test_pspace_vaddr_of_active_ptable()
{
    uint32_t vaddr = 0xc0000000;
    ptable_t* pt = _vmm_pspace_get_vaddr_of_active_ptable(vaddr);
    page_desc_t* ppage = vmm_ptable_lookup(pt, vaddr);
    page_desc_del_attrs(ppage, PAGE_DESC_PRESENT);
    uint32_t* kek1 = (uint32_t*)vaddr;
    *kek1 = 1;
    // should cause PF
    while (1) {
    }
    return true;
}
