/*
 * Copyright (C) 2020-2021 Nikita Melekhin. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifdef FPU_ENABLED
#include <drivers/x86/fpu.h>
#endif
#include <io/tty/tty.h>
#include <libkern/bits/errno.h>
#include <libkern/libkern.h>
#include <libkern/log.h>
#include <mem/kmalloc.h>
#include <platform/generic/system.h>
#include <tasking/cpu.h>
#include <tasking/dump.h>
#include <tasking/sched.h>
#include <tasking/tasking.h>
#include <tasking/thread.h>

#define TASKING_DEBUG

cpu_t cpus[CPU_CNT];
proc_t proc[MAX_PROCESS_COUNT];
uint32_t nxt_proc;

/**
 * used to jump to trapend
 * the jump will start the process
 */
#ifdef __i386__
void _tasking_jumper()
{
    cpu_leave_kernel_space();
    system_enable_interrupts_only_counter();
    return;
}
#endif

/**
 * TASK LOADING FUNCTIONS
 */

extern thread_t thread_storage[512];
extern int threads_cnt;
thread_t* tasking_get_thread(uint32_t tid)
{
    for (int i = 0; i < threads_cnt; i++) {
        if (thread_storage[i].tid == tid) {
            return &thread_storage[i];
        }
    }
    return 0;
}

proc_t* tasking_get_proc(uint32_t pid)
{
    proc_t* p;
    for (int i = 0; i < nxt_proc; i++) {
        p = &proc[i];
        if (p->pid == pid) {
            return p;
        }
    }
    return 0;
}

proc_t* tasking_get_proc_by_pdir(pdirectory_t* pdir)
{
    proc_t* p;
    for (int i = 0; i < nxt_proc; i++) {
        p = &proc[i];
        if (p->status == PROC_DEAD || p->status == PROC_DYING || p->status == PROC_INVALID) {
            continue;
        }

        if (p->pdir == pdir) {
            return p;
        }
    }
    return 0;
}

static proc_t* _tasking_alloc_proc()
{
    proc_t* p = &proc[nxt_proc++];
    proc_setup(p);
    return p;
}

static proc_t* _tasking_alloc_proc_with_uid(uid_t uid, gid_t gid)
{
    proc_t* p = &proc[nxt_proc++];
    proc_setup_with_uid(p, uid, gid);
    return p;
}

static proc_t* _tasking_fork_proc_from_current()
{
    proc_t* new_proc = _tasking_alloc_proc();
    new_proc->pdir = vmm_new_forked_user_pdir();
    proc_copy_of(new_proc, RUNNIG_THREAD);
    return new_proc;
}

static proc_t* _tasking_alloc_kernel_thread(void* entry_point)
{
    proc_t* p = &proc[nxt_proc++];
    kthread_setup(p);
    kthread_setup_regs(p, entry_point);
    return p;
}

/**
 * Start init proccess
 * All others processes will fork
 */
void tasking_start_init_proc()
{
    // We need to stop interrupts here, since this part of code
    // is NOT interruptable.
    system_disable_interrupts();
    proc_t* p = _tasking_alloc_proc_with_uid(0, 0);
    proc_setup_tty(p, tty_new());

    /* creating new pdir */
    p->pdir = vmm_new_user_pdir();

    if (proc_load(p, p->main_thread, "/boot/init") < 0) {
        log_error("Failed to load init proc");
        while (1) { }
    }

    sched_enqueue(p->main_thread);
    system_enable_interrupts();
}

proc_t* tasking_create_kernel_thread(void* entry_point, void* data)
{
    proc_t* p = _tasking_alloc_kernel_thread(entry_point);
    p->pdir = vmm_get_kernel_pdir();
    kthread_fill_up_stack(p->main_thread, data);
    p->main_thread->status = THREAD_RUNNING;
    sched_enqueue(p->main_thread);
    return p;
}

/**
 * TASKING RELATED FUNCTIONS
 */

void tasking_init()
{
    nxt_proc = 0;
    signal_init();
    dump_prepare_kernel_data();
}

void tasking_kill_dying()
{
    proc_t* p;
    for (int i = 0; i < nxt_proc; i++) {
        p = &proc[i];
        if (p->status == PROC_DYING) {
            proc_free(p);
            p->status = PROC_DEAD;
        }
    }
}

/**
 * SYSCALL IMPLEMENTATION
 */

void tasking_fork(trapframe_t* tf)
{
    proc_t* new_proc = _tasking_fork_proc_from_current();

    /* setting output */
    set_syscall_result(new_proc->main_thread->tf, 0);
    set_syscall_result(RUNNIG_THREAD->tf, new_proc->pid);

    new_proc->main_thread->status = THREAD_RUNNING;

#ifdef TASKING_DEBUG
    log("Fork %d to pid %d", RUNNIG_THREAD->tid, new_proc->pid);
#endif

    sched_enqueue(new_proc->main_thread);
    resched();
}

static int _tasking_do_exec(proc_t* p, thread_t* main_thread, const char* path, int argc, char** argv, char** env)
{
    int res = proc_load(p, main_thread, path);
    if (res == 0) {
        thread_fill_up_stack(p->main_thread, argc, argv, env);
    }
    return res;
}

/* TODO: Posix & zeroing-on-demand */
int tasking_exec(const char* path, const char** argv, const char** env)
{
    thread_t* thread = RUNNIG_THREAD;
    proc_t* p = RUNNIG_THREAD->process;
    char* kpath = NULL;
    int kargc = 0;
    char** kargv = NULL;
    char** kenv = NULL;

    if (!str_validate_len(path, 128)) {
        return -EINVAL;
    }
    kpath = kmem_bring_to_kernel(path, strlen(path) + 1);

    if (argv) {
        if (!ptrarr_validate_len(argv, 128)) {
            return -EINVAL;
        }
        kargc = ptrarr_len(argv);

        /* Validating arguments size */
        uint32_t data_len = 0;
        for (int argi = 0; argi < kargc; argi++) {
            if (!str_validate_len(argv[argi], 128)) {
                return -EINVAL;
            }
            data_len += strlen(argv[argi]) + 1;
            if (data_len > 128) {
                return -EINVAL;
            }
        }

        kargv = kmem_bring_to_kernel_ptrarr(argv, kargc);
    }

    int err = _tasking_do_exec(p, thread, kpath, kargc, kargv, 0);

#ifdef TASKING_DEBUG
    if (!err) {
        log("Exec %s : pid %d", kpath, p->pid);
    }
#endif

    kfree(kpath);
    for (int argi = 0; argi < kargc; argi++) {
        kfree(kargv[argi]);
    }
    kfree(kargv);

    return err;
}

int tasking_waitpid(int pid)
{
    thread_t* thread = RUNNIG_THREAD;
    thread_t* joinee_thread = tasking_get_thread(pid);
    if (!joinee_thread) {
        return -ESRCH;
    }
    thread->joinee = joinee_thread;
    init_join_blocker(thread);
    return 0;
}

void tasking_exit(int exit_code)
{
    proc_t* p = RUNNIG_THREAD->process;
    p->main_thread->exit_code = exit_code;
    proc_die(p);
    resched();
}

int tasking_kill(thread_t* thread, int signo)
{
    signal_set_pending(thread, signo);
    signal_dispatch_pending(thread);
    return 0;
}