/*
 * Copyright (c) 2016-2017 Wuklab, Purdue University. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _LEGO_MUTEX_H_
#define _LEGO_MUTEX_H_

#include <lego/types.h>
#include <lego/atomic.h>
#include <lego/spinlock.h>

/*
 * Simple, straightforward mutexes with strict semantics:
 *
 * - only one task can hold the mutex at a time
 * - only the owner can unlock the mutex
 * - multiple unlocks are not permitted
 * - recursive locking is not permitted
 * - a mutex object must be initialized via the API
 * - a mutex object must not be initialized via memset or copying
 * - task may not exit with mutex held
 * - memory areas where held locks reside must not be freed
 * - held mutexes must not be reinitialized
 * - mutexes may not be used in hardware or software interrupt
 *   contexts such as tasklets and timers
 *
 * These semantics are fully enforced when DEBUG_MUTEXES is
 * enabled. Furthermore, besides enforcing the above rules, the mutex
 * debugging code also implements a number of additional features
 * that make lock debugging easier and faster:
 *
 * - uses symbolic names of mutexes, whenever they are printed in debug output
 * - point-of-acquire tracking, symbolic lookup of function names
 * - list of all locks held in the system, printout of them
 * - owner tracking
 * - detects self-recursing locks and prints out all relevant info
 * - detects multi-task circular deadlocks and prints out all affected
 *   locks and tasks (and only those tasks)
 */
struct mutex {
	atomic_long_t		owner;
	spinlock_t		wait_lock;
	struct list_head	wait_list;
#ifdef CONFIG_DEBUG_MUTEXES
	void			*magic;
#endif
};

static inline struct task_struct *__mutex_owner(struct mutex *lock)
{
	return (struct task_struct *)(atomic_long_read(&lock->owner) & ~0x07);
}

struct mutex_waiter {
	struct list_head	list;
	struct task_struct	*task;
#ifdef CONFIG_DEBUG_MUTEXES
	void			*magic;
#endif
};

#ifdef CONFIG_DEBUG_MUTEXES
#define __DEBUG_MUTEX_INITIALIZER(lockname) \
	, .magic = &lockname
void mutex_destroy(struct mutex *lock);
#else
#define __DEBUG_MUTEX_INITIALIZER(lockname)
static inline void mutex_destroy(struct mutex *lock) { }
#endif

void __mutex_init(struct mutex *lock, const char *name);

/**
 * mutex_init - initialize the mutex
 * @mutex: the mutex to be initialized
 *
 * Initialize the mutex to unlocked state.
 *
 * It is not allowed to initialize an already locked mutex.
 */
#define mutex_init(mutex)		\
do {					\
	__mutex_init((mutex), #mutex);	\
} while (0)

#define __MUTEX_INITIALIZER(lockname)				\
{								\
	.owner = ATOMIC_LONG_INIT(0),				\
	.wait_lock = __SPIN_LOCK_UNLOCKED(lockname.wait_lock),	\
	.wait_list = LIST_HEAD_INIT(lockname.wait_list)		\
	__DEBUG_MUTEX_INITIALIZER(lockname)			\
}

#define DEFINE_MUTEX(mutexname) \
	struct mutex mutexname = __MUTEX_INITIALIZER(mutexname)

/**
 * mutex_is_locked - is the mutex locked
 * @lock: the mutex to be queried
 *
 * Returns 1 if the mutex is locked, 0 if unlocked.
 */
static inline int mutex_is_locked(struct mutex *lock)
{
	return __mutex_owner(lock) != NULL;
}

/*
 * NOTE: mutex_trylock() follows the spin_trylock() convention,
 *       not the down_trylock() convention!
 *
 * Returns 1 if the mutex has been acquired successfully, and 0 on contention.
 */
int mutex_trylock(struct mutex *lock);
void mutex_unlock(struct mutex *lock);
void mutex_lock(struct mutex *lock);

#endif /* _LEGO_MUTEX_H_ */
