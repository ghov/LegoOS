#include <lego/kernel.h>
#include <lego/sched.h>
#include <lego/init.h>
//#include <lego/completion.h>
#include <lego/workqueue.h>
#include <lego/slab.h>
//#include <lego/kthread.h>
//#include <lego/hardirq.h>

enum {
	/*
	 * worker_pool flags
	 *
	 * A bound pool is either associated or disassociated with its CPU.
	 * While associated (!DISASSOCIATED), all workers are bound to the
	 * CPU and none has %WORKER_UNBOUND set and concurrency management
	 * is in effect.
	 *
	 * While DISASSOCIATED, the cpu may be offline and all workers have
	 * %WORKER_UNBOUND set and concurrency management disabled, and may
	 * be executing on any CPU.  The pool behaves as an unbound one.
	 *
	 * Note that DISASSOCIATED should be flipped only while holding
	 * manager_mutex to avoid changing binding state while
	 * create_worker() is in progress.
	 */
	POOL_MANAGE_WORKERS	= 1 << 0,	/* need to manage workers */
	POOL_DISASSOCIATED	= 1 << 2,	/* cpu can't serve workers */
	POOL_FREEZING		= 1 << 3,	/* freeze in progress */

	/* worker flags */
	WORKER_STARTED		= 1 << 0,	/* started */
	WORKER_DIE		= 1 << 1,	/* die die die */
	WORKER_IDLE		= 1 << 2,	/* is idle */
	WORKER_PREP		= 1 << 3,	/* preparing to run works */
	WORKER_CPU_INTENSIVE	= 1 << 6,	/* cpu intensive */
	WORKER_UNBOUND		= 1 << 7,	/* worker is unbound */
	WORKER_REBOUND		= 1 << 8,	/* worker was rebound */

	WORKER_NOT_RUNNING	= WORKER_PREP | WORKER_CPU_INTENSIVE |
				  WORKER_UNBOUND | WORKER_REBOUND,

	NR_STD_WORKER_POOLS	= 2,		/* # standard pools per cpu */

	UNBOUND_POOL_HASH_ORDER	= 6,		/* hashed by pool->attrs */
	BUSY_WORKER_HASH_ORDER	= 6,		/* 64 pointers */

	MAX_IDLE_WORKERS_RATIO	= 4,		/* 1/4 of busy can be idle */

	WQ_NAME_LEN		= 24,
};

/*
 * Structure fields follow one of the following exclusion rules.
 *
 * I: Modifiable by initialization/destruction paths and read-only for
 *    everyone else.
 *
 * P: Preemption protected.  Disabling preemption is enough and should
 *    only be modified and accessed from the local cpu.
 *
 * L: pool->lock protected.  Access with pool->lock held.
 *
 * X: During normal operation, modification requires pool->lock and should
 *    be done only from local cpu.  Either disabling preemption on local
 *    cpu or grabbing pool->lock is enough for read access.  If
 *    POOL_DISASSOCIATED is set, it's identical to L.
 *
 * MG: pool->manager_mutex and pool->lock protected.  Writes require both
 *     locks.  Reads can happen under either lock.
 *
 * PL: wq_pool_lock protected.
 *
 * PR: wq_pool_lock protected for writes.  Sched-RCU protected for reads.
 *
 * WQ: wq->mutex protected.
 *
 * WR: wq->mutex protected for writes.  Sched-RCU protected for reads.
 *
 * MD: wq_mayday_lock protected.
 */

/* struct worker is defined in workqueue_internal.h */

static DEFINE_SPINLOCK(wq_pool_lock);	/* protects pools and workqueues list */
static LIST_HEAD(workqueues);		/* PL: list of all workqueues */

struct worker_pool {
	spinlock_t		lock;		/* the pool lock */
//	int			cpu;		/* I: the associated cpu */
//	int			node;		/* I: the associated node ID */
	int			id;		/* I: pool ID */
	unsigned int		flags;		/* X: flags */

	struct list_head	worklist;	/* L: list of pending works */
	int			nr_workers;	/* L: total number of workers */

	/* nr_idle includes the ones off idle_list for rebinding */
	int			nr_idle;	/* L: currently idle ones */

	struct list_head	idle_list;	/* X: list of idle workers */
//	struct timer_list	idle_timer;	/* L: worker idle timeout */

	/* see manage_workers() for details on the two manager mutexes */
//	struct mutex		manager_arb;	/* manager arbitration */
//	struct mutex		manager_mutex;	/* manager exclusion */
//	struct idr		worker_idr;	/* MG: worker IDs and iteration */

//	int			refcnt;		/* PL: refcnt for unbound pools */

	/*
	 * The current concurrency level.  As it's likely to be accessed
	 * from other CPUs during try_to_wake_up(), put it in a separate
	 * cacheline.
	 */
	atomic_t		nr_running;

	/*
	 * Destruction of pool is sched-RCU protected to allow dereferences
	 * from get_work_pool().
	 */
//	struct rcu_head		rcu;
}; // ____cacheline_aligned_in_smp;

/*
 * The externally visible workqueue.  It relays the issued work items to
 * the appropriate worker_pool through its pool_workqueues.
 */
struct workqueue_struct {
	struct list_head	pwqs;		/* WR: all pwqs of this wq */
	struct list_head	list;		/* PL: list of all workqueues */

	struct mutex		mutex;		/* protects this wq */
	atomic_t		nr_pwqs_to_flush; /* flush in progress */
	//struct wq_flusher	*first_flusher;	/* WQ: first flusher */
	struct list_head	flusher_queue;	/* WQ: flush waiters */
	//struct list_head	flusher_overflow; /* WQ: flush overflow list */

	//struct list_head	maydays;	/* MD: pwqs requesting rescue */

	int			nr_drainers;	/* WQ: drain in progress */
	int			saved_max_active; /* WQ: saved pwq max_active */

	/* hot fields used during command issue, aligned to cacheline */
	unsigned int		flags ____cacheline_aligned; /* WQ: WQ_* flags */
	struct pool_workqueue  *global_pwq; /* I: global pwq */
};

/*
* The per-pool workqueue.  While queued, the lower WORK_STRUCT_FLAG_BITS
* of work_struct->data are used for flags and the remaining high bits
* point to the pwq; thus, pwqs need to be aligned at two's power of the
* number of flag bits.
*/
struct pool_workqueue {
       struct worker_pool	*pool;		/* I: the associated pool */
       struct workqueue_struct	*wq;		/* I: the owning workqueue */
       int			work_color;	/* L: current color */
       int			flush_color;	/* L: flushing color */
       int			refcnt;		/* L: reference count */
//       int			nr_in_flight[WORK_NR_COLORS];
       int			nr_active;	/* L: nr of active works */
       int			max_active;	/* L: max active works */
       struct list_head	delayed_works;	/* L: delayed works */
}; // __aligned(1 << WORK_STRUCT_FLAG_BITS);

/*
 * The poor guys doing the actual heavy lifting.  All on-duty workers are
 * either serving the manager role, on idle list or on busy hash.  For
 * details on the locking annotation (L, I, X...), refer to workqueue.c.
 *
 */
struct worker {
	/* on idle list while idle, on busy hash table while busy */
//	union {
		struct list_head	entry;	/* L: while idle/busy */
//		struct hlist_node	hentry;	/* L: while busy */
//	};

	struct work_struct	*current_work;	/* L: work being processed */
	work_func_t		current_func;	/* L: current_work's fn */
//	struct pool_workqueue	*current_pwq; /* L: current_work's pwq */
	bool			desc_valid;	/* ->desc is valid */
	struct list_head	scheduled;	/* L: scheduled works */

	/* 64 bytes boundary on 64bit, 32 on 32bit */

	struct task_struct	*task;		/* I: worker task */
	struct worker_pool	*pool;		/* I: the associated pool */
						/* L: for rescuers */

	unsigned long		last_active;	/* L: last active timestamp */
	unsigned int		flags;		/* X: flags */
	int			id;		/* I: worker id */
};

struct worker_pool *global_pool;

/*
 * Policy functions.  These define the policies on how the global worker
 * pools are managed.  Unless noted otherwise, these functions assume that
 * they're being called with pool->lock held.
 */

static bool __need_more_worker(struct worker_pool *pool)
{
	return !atomic_read(&pool->nr_running);
}

/*
 * Need to wake up a worker?  Called from anything but currently
 * running workers.
 *
 * Note that, because unbound workers never contribute to nr_running, this
 * function will always return %true for unbound pools as long as the
 * worklist isn't empty.
 */
static bool need_more_worker(struct worker_pool *pool)
{
	return !list_empty(&pool->worklist) && __need_more_worker(pool);
}

/* Can I start working?  Called from busy but !running workers. */
static bool may_start_working(struct worker_pool *pool)
{
	return pool->nr_idle;
}

/* Do I need to keep working?  Called from currently running workers. */
static bool keep_working(struct worker_pool *pool)
{
	return !list_empty(&pool->worklist) &&
		atomic_read(&pool->nr_running) <= 1;
}

/* Do we need a new worker?  Called from manager. */
static bool need_to_create_worker(struct worker_pool *pool)
{
	return need_more_worker(pool) && !may_start_working(pool);
}

#if 0
/* Do I need to be the manager? */
static bool need_to_manage_workers(struct worker_pool *pool)
{
	return need_to_create_worker(pool) ||
		(pool->flags & POOL_MANAGE_WORKERS);
}

/* Do we have too many workers and should some go away? */
static bool too_many_workers(struct worker_pool *pool)
{
	bool managing = mutex_is_locked(&pool->manager_arb);
	int nr_idle = pool->nr_idle + managing; /* manager is considered idle */
	int nr_busy = pool->nr_workers - nr_idle;

	/*
	 * nr_idle and idle_list may disagree if idle rebinding is in
	 * progress.  Never return %true if idle_list is empty.
	 */
	if (list_empty(&pool->idle_list))
		return false;

	return nr_idle > 2 && (nr_idle - 2) * MAX_IDLE_WORKERS_RATIO >= nr_busy;
}
#endif

/*
 * Wake up functions.
 */

/* Return the first worker.  Safe with preemption disabled */
static struct worker *first_worker(struct worker_pool *pool)
{
	if (unlikely(list_empty(&pool->idle_list)))
		return NULL;

	return list_first_entry(&pool->idle_list, struct worker, entry);
}

/**
 * wake_up_worker - wake up an idle worker
 * @pool: worker pool to wake worker from
 *
 * Wake up the first idle worker of @pool.
 *
 * CONTEXT:
 * spin_lock_irq(pool->lock).
 */
static void wake_up_worker(struct worker_pool *pool)
{
	struct worker *worker = first_worker(pool);

	if (likely(worker))
		wake_up_process(worker->task);
}

static int alloc_and_link_pwqs(struct workqueue_struct *wq)
{
	bool highpri = wq->flags & WQ_HIGHPRI;
	int cpu;

	wq->global_pwq = (struct pool_workqueue *)kzalloc(sizeof(struct pool_workqueue), GFP_KERNEL);
	if (!wq->global_pwq)
		return -ENOMEM;

	pwq->pool = global_pool;
	pwq->wq = wq;
	pwq->refcnt = 1;
	pwq->max_active = 1;
	INIT_LIST_HEAD(&pwq->delayed_works);

	//spin_lock(&wq->mutex);
	//pwq->work_color = wq->work_color;
	//pwq_adjust_max_active(pwq);
	/* link in @pwq */
	//list_add_rcu(&pwq->pwqs_node, &wq->pwqs);
	//spin_unlock(&wq->mutex);

	return 0;
}

struct workqueue_struct *__alloc_workqueue(unsigned int flags,
					       int max_active)
{
	size_t tbl_size = 0;
	struct workqueue_struct *wq;
	struct pool_workqueue *pwq;

	wq = kzalloc(sizeof(*wq) + tbl_size, GFP_KERNEL);
	if (!wq)
		return NULL;

	max_active = max_active ?: WQ_DFL_ACTIVE;
	max_active = wq_clamp_max_active(max_active, flags, wq->name);

	/* init wq */
	wq->flags = flags;
	wq->saved_max_active = max_active;
	mutex_init(&wq->mutex);
	atomic_set(&wq->nr_pwqs_to_flush, 0);
	INIT_LIST_HEAD(&wq->pwqs);
	INIT_LIST_HEAD(&wq->list);

	if (alloc_and_link_pwqs(wq) < 0)
		goto err_free_wq;

	/*
	 * wq_pool_lock protects global freeze state and workqueues list.
	 * Grab it, adjust max_active and add the new @wq to workqueues
	 * list.
	 */
	spin_lock(&wq_pool_lock);

	list_add(&wq->list, &workqueues);

	spin_unlock(&wq_pool_lock);

	return wq;

err_free_wq:
	kfree(wq);
	return NULL;
err_destroy:
	destroy_workqueue(wq);
	return NULL;
}

static inline void set_work_data(struct work_struct *work, unsigned long data,
				 unsigned long flags)
{
	//WARN_ON_ONCE(!work_pending(work));
	atomic_set(&work->data, data | flags | work_static(work));
}

static void set_work_pwq(struct work_struct *work, struct pool_workqueue *pwq,
			 unsigned long extra_flags)
{
	set_work_data(work, (unsigned long)pwq,
		      WORK_STRUCT_PENDING | WORK_STRUCT_PWQ | extra_flags);
}

/**
 * insert_work - insert a work into a pool
 * @pwq: pwq @work belongs to
 * @work: work to insert
 * @head: insertion point
 * @extra_flags: extra WORK_STRUCT_* flags to set
 *
 * Insert @work which belongs to @pwq after @head.  @extra_flags is or'd to
 * work_struct flags.
 *
 * CONTEXT:
 * spin_lock_irq(pool->lock).
 */
static void insert_work(struct pool_workqueue *pwq, struct work_struct *work,
			struct list_head *head, unsigned int extra_flags)
{
	struct worker_pool *pool = pwq->pool;

	/* we own @work, set data and link */
	set_work_pwq(work, pwq, extra_flags);
	list_add_tail(&work->entry, head);
	pwq->refcnt++;

	/*
	 * Ensure either wq_worker_sleeping() sees the above
	 * list_add_tail() or we see zero nr_running to avoid workers lying
	 * around lazily while there are works to be processed.
	 */
	smp_mb();

	if (__need_more_worker(pool))
		wake_up_worker(pool);
}

static void __queue_work(struct workqueue_struct *wq,
			 struct work_struct *work)
{
	struct pool_workqueue *pwq;
	struct list_head *worklist;
	unsigned int work_flags;
	unsigned int req_cpu = cpu;

	/*
	 * While a work item is PENDING && off queue, a task trying to
	 * steal the PENDING will busy-loop waiting for it to either get
	 * queued or lose PENDING.  Grabbing PENDING and queueing should
	 * happen with IRQ disabled.
	 */
	//WARN_ON_ONCE(!irqs_disabled());

retry:
	pwq = wq->global_pwq;

	/* pwq determined, queue */
	//trace_workqueue_queue_work(req_cpu, pwq, work);

	if (WARN_ON(!list_empty(&work->entry))) {
		return;
	}

//	pwq->nr_in_flight[pwq->work_color]++;
	work_flags = work_color_to_flags(pwq->work_color);

	if (likely(pwq->nr_active < pwq->max_active)) {
		//trace_workqueue_activate_work(work);
		pwq->nr_active++;
		worklist = &pwq->pool->worklist;
	} else {
		work_flags |= WORK_STRUCT_DELAYED;
		worklist = &pwq->delayed_works;
	}

	insert_work(pwq, work, worklist, work_flags);
}

/**
 * queue_work - queue work
 * @wq: workqueue to use
 * @work: work to queue
 *
 * Returns %false if @work was already on a queue, %true otherwise.
 *
 * We queue the work to a specific CPU, the caller must ensure it
 * can't go away.
 */
bool queue_work(struct workqueue_struct *wq,
		   struct work_struct *work)
{
	bool ret = false;
	unsigned long flags;

	local_irq_save(flags);

	if (!test_and_set_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work))) {
		__queue_work(wq, work);
		ret = true;
	}

	local_irq_restore(flags);
	return ret;
}

/**
 * process_one_work - process single work
 * @worker: self
 * @work: work to process
 *
 * Process @work.  This function contains all the logics necessary to
 * process a single work including synchronization against and
 * interaction with other workers on the same cpu, queueing and
 * flushing.  As long as context requirement is met, any worker can
 * call this function to process a work.
 *
 * CONTEXT:
 * spin_lock_irq(pool->lock) which is released and regrabbed.
 */
static void process_one_work(struct worker *worker, struct work_struct *work)
__releases(&pool->lock)
__acquires(&pool->lock)
{
	struct pool_workqueue *pwq = get_work_pwq(work);
	struct worker_pool *pool = worker->pool;
	bool cpu_intensive = pwq->wq->flags & WQ_CPU_INTENSIVE;
	int work_color;
	struct worker *collision;

	/* claim and dequeue */
	worker->current_work = work;
	worker->current_func = work->func;
	worker->current_pwq = pwq;
	work_color = get_work_color(work);

	list_del_init(&work->entry);

	if (need_more_worker(pool))
		wake_up_worker(pool);

	/*
	 * Record the last pool and clear PENDING which should be the last
	 * update to @work.  Also, do this inside @pool->lock so that
	 * PENDING and queued state changes happen together while IRQ is
	 * disabled.
	 */
	set_work_pool_and_clear_pending(work, pool->id);

	spin_unlock_irq(&pool->lock);

	lock_map_acquire_read(&pwq->wq->lockdep_map);
	lock_map_acquire(&lockdep_map);
	//trace_workqueue_execute_start(work);
	worker->current_func(work);
	/*
	 * While we must be careful to not use "work" after this, the trace
	 * point will only record its address.
	 */
	//trace_workqueue_execute_end(work);

	/*
	 * The following prevents a kworker from hogging CPU on !PREEMPT
	 * kernels, where a requeueing work item waiting for something to
	 * happen could deadlock with stop_machine as such work item could
	 * indefinitely requeue itself while all other CPUs are trapped in
	 * stop_machine.
	 */
	//cond_resched();

	spin_lock_irq(&pool->lock);

	/* we're done with it, release */
	worker->current_work = NULL;
	worker->current_func = NULL;
	worker->current_pwq = NULL;
	worker->desc_valid = false;
	pwq->nr_active--;
	//pwq_dec_nr_in_flight(pwq, work_color);
}

/**
 * worker_clr_flags - clear worker flags and adjust nr_running accordingly
 * @worker: self
 * @flags: flags to clear
 *
 * Clear @flags in @worker->flags and adjust nr_running accordingly.
 *
 * CONTEXT:
 * spin_lock_irq(pool->lock)
 */
static inline void worker_clr_flags(struct worker *worker, unsigned int flags)
{
	struct worker_pool *pool = worker->pool;
	unsigned int oflags = worker->flags;

	//WARN_ON_ONCE(worker->task != current);

	worker->flags &= ~flags;

	/*
	 * If transitioning out of NOT_RUNNING, increment nr_running.  Note
	 * that the nested NOT_RUNNING is not a noop.  NOT_RUNNING is mask
	 * of multiple flags, not a single flag.
	 */
	if ((flags & WORKER_NOT_RUNNING) && (oflags & WORKER_NOT_RUNNING))
		if (!(worker->flags & WORKER_NOT_RUNNING))
			atomic_inc(&pool->nr_running);
}

/**
 * worker_leave_idle - leave idle state
 * @worker: worker which is leaving idle state
 *
 * @worker is leaving idle state.  Update stats.
 *
 * LOCKING:
 * spin_lock_irq(pool->lock).
 */
static void worker_leave_idle(struct worker *worker)
{
	struct worker_pool *pool = worker->pool;

	if (W!(worker->flags & WORKER_IDLE))
		return;
	worker_clr_flags(worker, WORKER_IDLE);
	pool->nr_idle--;
	list_del_init(&worker->entry);
}

/**
 * worker_thread - the worker thread function
 * @__worker: self
 *
 * The worker thread function.  All workers belong to a worker_pool -
 * either a per-cpu one or dynamic unbound one.  These workers process all
 * work items regardless of their specific target workqueue.  The only
 * exception is work items which belong to workqueues with a rescuer which
 * will be explained in rescuer_thread().
 */
static int worker_thread(void *__worker)
{
	struct worker *worker = __worker;
	struct worker_pool *pool = worker->pool;

	/* tell the scheduler that this is a workqueue worker */
//	worker->task->flags |= PF_WQ_WORKER;
woke_up:
	spin_lock_irq(&pool->lock);

	worker_leave_idle(worker);
recheck:
	/* no more worker necessary? */
	if (!need_more_worker(pool))
		goto sleep;

	/* do we need to manage? */
	//if (unlikely(!may_start_working(pool)) && manage_workers(worker))
	//	goto recheck;

	/*
	 * ->scheduled list can only be filled while a worker is
	 * preparing to process a work or actually processing it.
	 * Make sure nobody diddled with it while I was sleeping.
	 */
	//WARN_ON_ONCE(!list_empty(&worker->scheduled));

	/*
	 * Finish PREP stage.  We're guaranteed to have at least one idle
	 * worker or that someone else has already assumed the manager
	 * role.  This is where @worker starts participating in concurrency
	 * management if applicable and concurrency management is restored
	 * after being rebound.  See rebind_workers() for details.
	 */
	worker_clr_flags(worker, WORKER_PREP | WORKER_REBOUND);

	do {
		struct work_struct *work =
			list_first_entry(&pool->worklist,
					 struct work_struct, entry);

//		if (likely(!(*work_data_bits(work) & WORK_STRUCT_LINKED))) {
			/* optimization path, not strictly necessary */
			process_one_work(worker, work);
//			if (unlikely(!list_empty(&worker->scheduled)))
//				process_scheduled_works(worker);
//		} else {
//			move_linked_works(work, &worker->scheduled, NULL);
//			process_scheduled_works(worker);
		}
	} while (keep_working(pool));

	worker_set_flags(worker, WORKER_PREP, false);
sleep:
//	if (unlikely(need_to_manage_workers(pool)) && manage_workers(worker))
//		goto recheck;

	/*
	 * pool->lock is held and there's no work to process and no need to
	 * manage, sleep.  Workers are woken up only while holding
	 * pool->lock or from local cpu, so setting the current state
	 * before releasing pool->lock is enough to prevent losing any
	 * event.
	 */
	worker_enter_idle(worker);
	__set_current_state(TASK_INTERRUPTIBLE);
	spin_unlock_irq(&pool->lock);
	schedule();
	goto woke_up;
}

/**
 * worker_enter_idle - enter idle state
 * @worker: worker which is entering idle state
 *
 * @worker is entering idle state.  Update stats and idle timer if
 * necessary.
 *
 * LOCKING:
 * spin_lock_irq(pool->lock).
 */
static void worker_enter_idle(struct worker *worker)
{
	struct worker_pool *pool = worker->pool;

	if ((worker->flags & WORKER_IDLE) ||
	    (!list_empty(&worker->entry) &&
			 (worker->hentry.next || worker->hentry.pprev)))
		return;

	/* can't use worker_set_flags(), also called from start_worker() */
	worker->flags |= WORKER_IDLE;
	pool->nr_idle++;
	worker->last_active = jiffies;

	/* idle_list is LIFO */
	list_add(&worker->entry, &pool->idle_list);
}

/**
 * create_and_start_worker - create and start a worker for a pool
 * @pool: the target pool
 *
 * Grab the managership of @pool and create and start a new worker for it.
 */
static int create_and_start_worker(struct worker_pool *pool)
{
	struct worker *worker = NULL;
	int id = -1;

	worker = kzalloc(sizeof(*worker), GFP_KERNEL);
	if (!worker)
		return -ENOMEM;

	INIT_LIST_HEAD(&worker->entry);
	INIT_LIST_HEAD(&worker->scheduled);
	/* on creation a worker is in !idle && prep state */
	worker->flags = WORKER_PREP;

	worker->pool = pool;
	worker->id = id;

	worker->task = kthread_create(worker_thread, worker, 
					      "kworker/%s", id_buf);
	if (IS_ERR(worker->task))
		kfree(worker);
		return 0;

	worker->flags |= WORKER_STARTED;
	worker->pool->nr_workers++;
	worker_enter_idle(worker);
	wake_up_process(worker->task);

	return 0;
}

/**
 * init_worker_pool - initialize a newly zalloc'd worker_pool
 * @pool: worker_pool to initialize
 *
 * Initiailize a newly zalloc'd @pool.  It also allocates @pool->attrs.
 * Returns 0 on success, -errno on failure.  Even on failure, all fields
 * inside @pool proper are initialized and put_unbound_pool() can be called
 * on @pool safely to release it.
 */
static int init_worker_pool(struct worker_pool *pool)
{
	spin_lock_init(&pool->lock);
	pool->id = 0;
	pool->flags |= POOL_DISASSOCIATED;
	INIT_LIST_HEAD(&pool->worklist);
	INIT_LIST_HEAD(&pool->idle_list);

//	init_timer_deferrable(&pool->idle_timer);
//	pool->idle_timer.function = idle_worker_timeout;
//	pool->idle_timer.data = (unsigned long)pool;

//	mutex_init(&pool->manager_arb);
//	mutex_init(&pool->manager_mutex);
//	idr_init(&pool->worker_idr);

	pool->refcnt = 1;

	return 0;
}

int init_workqueues(void)
{
	global_pool = (struct work_pool *)kmalloc(sizeof(struct work_pool), GFP_KERNEL);

	BUG_ON(init_worker_pool(global_pool));

	/* create the initial worker */
	global_pool->flags &= ~POOL_DISASSOCIATED;
	BUG_ON(create_and_start_worker(global_pool) < 0);

	//system_wq = __alloc_workqueue(0, 0);
	//BUG_ON(!system_wq);

	return 0;
}