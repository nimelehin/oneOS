/*
 * Copyright (C) 2020 Nikita Melekhin
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 */

#include <platform/generic/init.h>
#include <platform/generic/registers.h>
#include <platform/generic/system.h>

#include <types.h>

#include <drivers/driver_manager.h>

#include <mem/kmalloc.h>
#include <mem/pmm.h>

#include <fs/devfs/devfs.h>
#include <fs/ext2/ext2.h>
#include <fs/procfs/procfs.h>
#include <fs/vfs.h>

#include <io/shared_buffer/shared_buffer.h>
#include <io/tty/ptmx.h>
#include <io/tty/tty.h>

#include <time/time_manager.h>

#include <tasking/sched.h>

#include <log.h>
#include <utils/kernel_self_test.h>

#include <sys_handler.h>

/* If we stay our anythread alone it can't get keyboard input,
   so we will switch to idle_thread to fix it. */
void idle_thread()
{
    while (1) { }
}

void launching()
{
    tasking_create_kernel_thread(idle_thread);
    tasking_create_kernel_thread(dentry_flusher);
    tasking_start_init_proc();
    ksys1(SYSEXIT, 0);
}

void stage3(mem_desc_t* mem_desc)
{
    system_disable_interrupts();
    logger_setup();
    platform_setup();

    // mem setup
    pmm_setup(mem_desc);
    vmm_setup();

    timeman_setup();

    // installing drivers
    driver_manager_init();
    platform_drivers_setup();
    vfs_install();
    ext2_install();
    procfs_install();
    devfs_install();
    drivers_run();

    // mounting filesystems
    devfs_mount();

    // ipc
    shared_buffer_init();

    // pty
    ptmx_install();

    // init scheduling
    tasking_init();
    scheduler_init();
    tasking_create_kernel_thread(launching);
    resched(); /* Starting a scheduler */

    while (1) { }
}