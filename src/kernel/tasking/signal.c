/*
 * Copyright (C) 2020 Nikita Melekhin
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 */

#include <mem/vmm/vmm.h>
#include <mem/vmm/zoner.h>
#include <tasking/tasking.h>
#include <utils/kassert.h>

static uint32_t _signal_caller;

extern void signal_caller_start();
extern void signal_caller_end();

/**
 * INIT
 */

static void _signal_init_caller()
{
    _signal_caller = zoner_new_vzone(VMM_PAGE_SIZE);
    vmm_load_page(_signal_caller, USER_PAGE);
    uint32_t signal_caller_len = (uint32_t)signal_caller_end - (uint32_t)signal_caller_start;
    memcpy((void*)_signal_caller, (void*)signal_caller_start, signal_caller_len);
}

void signal_init()
{
    _signal_init_caller();
}

/**
 * HELPER STACK FUNCTIONS
 */

static inline void signal_push_to_user_stack(proc_t* proc, uint32_t value)
{
    proc->tf->esp -= 4;
    *((uint32_t*)proc->tf->esp) = value;
}

static inline uint32_t signal_pop_from_user_stack(proc_t* proc)
{
    uint32_t val = *((uint32_t*)proc->tf->esp);
    proc->tf->esp += 4;
    return val;
}

/**
 * HELPER FUNCTIONS
 */

int signal_set_handler(proc_t* proc, int signo, void* handler)
{
    if (signo < 0 || signo >= SIGNALS_CNT) {
        return -1;
    }
    proc->signal_handlers[signo] = handler;
    return 0;
}

/**
 * Makes signal allow to be called
 */
int signal_set_allow(proc_t* proc, int signo)
{
    if (signo < 0 || signo >= SIGNALS_CNT) {
        return -1;
    }
    proc->signals_mask |= (1 << signo);
    return 0;
}

/**
 * Makes signal private. It can't be called.
 */
int signal_set_private(proc_t* proc, int signo)
{
    if (signo < 0 || signo >= SIGNALS_CNT) {
        return -1;
    }
    proc->signals_mask &= ~(1 << signo);
    return 0;
}

int signal_set_pending(proc_t* proc, int signo)
{
    if (signo < 0 || signo >= SIGNALS_CNT) {
        return -1;
    }
    proc->pending_signals_mask |= (1 << signo);
    return 0;
}

static int signal_setup_stack_to_handle_signal(proc_t* proc, int signo)
{
    uint32_t old_esp = proc->tf->esp;
    signal_push_to_user_stack(proc, proc->tf->eflags);
    signal_push_to_user_stack(proc, proc->tf->eip);
    signal_push_to_user_stack(proc, proc->tf->eax);
    signal_push_to_user_stack(proc, proc->tf->ebx);
    signal_push_to_user_stack(proc, proc->tf->ecx);
    signal_push_to_user_stack(proc, proc->tf->edx);
    signal_push_to_user_stack(proc, old_esp);
    signal_push_to_user_stack(proc, proc->tf->ebp);
    signal_push_to_user_stack(proc, proc->tf->esi);
    signal_push_to_user_stack(proc, proc->tf->edi);
    signal_push_to_user_stack(proc, (uint32_t)proc->signal_handlers[signo]);
    signal_push_to_user_stack(proc, (uint32_t)signo);
    signal_push_to_user_stack(proc, 0); /* fake return address */
    return 0;
}

int signal_restore_proc_after_handling_signal(proc_t* proc)
{
    int ret = proc->tf->ebx;
    proc->tf->esp += 12; /* cleaning 3 last pushes */

    proc->tf->edi = signal_pop_from_user_stack(proc);
    proc->tf->esi = signal_pop_from_user_stack(proc);
    proc->tf->ebp = signal_pop_from_user_stack(proc);
    uint32_t old_esp = signal_pop_from_user_stack(proc);
    proc->tf->edx = signal_pop_from_user_stack(proc);
    proc->tf->ecx = signal_pop_from_user_stack(proc);
    proc->tf->ebx = signal_pop_from_user_stack(proc);
    proc->tf->eax = signal_pop_from_user_stack(proc);
    proc->tf->eip = signal_pop_from_user_stack(proc);
    proc->tf->eflags = signal_pop_from_user_stack(proc);

    if (old_esp != proc->tf->esp) {
        kpanic("ESPs are diff after signal");
    }

    return ret;
}

static int signal_process(proc_t* proc, int signo)
{
    if (proc->signal_handlers[signo]) {
        signal_setup_stack_to_handle_signal(proc, signo);
        proc->tf->eip = _signal_caller;
        return 0;
    }
    return -1;
}

int signal_dispatch_pending(proc_t* proc)
{
    uint32_t candidate_signals = proc->pending_signals_mask & proc->signals_mask;

    if (!candidate_signals) {
        return -1;
    }

    uint32_t signo = 1;
    for (; signo < 32; signo++) {
        if (candidate_signals & (1 << signo)) {
            break;
        }
    }
    return signal_process(proc, signo);
}
