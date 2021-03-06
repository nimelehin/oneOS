/*
 * Copyright (C) 2020-2021 Nikita Melekhin. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _KERNEL_TASKING_BITS_SCHED_H
#define _KERNEL_TASKING_BITS_SCHED_H

#define MAX_PRIO 0
#define MIN_PRIO 11
#define IDLE_PRIO (MIN_PRIO + 1)
#define PROC_PRIOS_COUNT (MIN_PRIO - MAX_PRIO + 1)
#define TOTAL_PRIOS_COUNT (IDLE_PRIO - MAX_PRIO + 1)
#define DEFAULT_PRIO 6
#define SCHED_INT 10

#endif // _KERNEL_TASKING_BITS_SCHED_H