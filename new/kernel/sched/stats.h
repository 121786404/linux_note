/* SPDX-License-Identifier: GPL-2.0 */

#ifdef CONFIG_SCHEDSTATS

/*
 * Expects runqueue lock to be held for atomicity of update
 */
static inline void
rq_sched_info_arrive(struct rq *rq, unsigned long long delta)
{
	if (rq) {
		rq->rq_sched_info.run_delay += delta;
		rq->rq_sched_info.pcount++;
	}
}

/*
 * Expects runqueue lock to be held for atomicity of update
 */
static inline void
rq_sched_info_depart(struct rq *rq, unsigned long long delta)
{
	if (rq)
		rq->rq_cpu_time += delta;
}

static inline void
rq_sched_info_dequeued(struct rq *rq, unsigned long long delta)
{
	if (rq)
		rq->rq_sched_info.run_delay += delta;
}
#define   schedstat_enabled()		static_branch_unlikely(&sched_schedstats)
#define __schedstat_inc(var)		do { var++; } while (0)
#define   schedstat_inc(var)		do { if (schedstat_enabled()) { var++; } } while (0)
#define __schedstat_add(var, amt)	do { var += (amt); } while (0)
#define   schedstat_add(var, amt)	do { if (schedstat_enabled()) { var += (amt); } } while (0)
#define __schedstat_set(var, val)	do { var = (val); } while (0)
#define   schedstat_set(var, val)	do { if (schedstat_enabled()) { var = (val); } } while (0)
#define   schedstat_val(var)		(var)
#define   schedstat_val_or_zero(var)	((schedstat_enabled()) ? (var) : 0)

#else /* !CONFIG_SCHEDSTATS: */
static inline void rq_sched_info_arrive  (struct rq *rq, unsigned long long delta) { }
static inline void rq_sched_info_dequeued(struct rq *rq, unsigned long long delta) { }
static inline void rq_sched_info_depart  (struct rq *rq, unsigned long long delta) { }
# define   schedstat_enabled()		0
# define __schedstat_inc(var)		do { } while (0)
# define   schedstat_inc(var)		do { } while (0)
# define __schedstat_add(var, amt)	do { } while (0)
# define   schedstat_add(var, amt)	do { } while (0)
# define __schedstat_set(var, val)	do { } while (0)
# define   schedstat_set(var, val)	do { } while (0)
# define   schedstat_val(var)		0
# define   schedstat_val_or_zero(var)	0
#endif /* CONFIG_SCHEDSTATS */

#ifdef CONFIG_SCHED_INFO
static inline void sched_info_reset_dequeued(struct task_struct *t)
{
	t->sched_info.last_queued = 0;
}

/*
 * We are interested in knowing how long it was from the *first* time a
 * task was queued to the time that it finally hit a CPU, we call this routine
 * from dequeue_task() to account for possible rq->clock skew across CPUs. The
 * delta taken on each CPU would annul the skew.
 */
static inline void sched_info_dequeued(struct rq *rq, struct task_struct *t)
{
	unsigned long long now = rq_clock(rq), delta = 0;

	if (unlikely(sched_info_on()))
		if (t->sched_info.last_queued)
			delta = now - t->sched_info.last_queued;
	sched_info_reset_dequeued(t);
	t->sched_info.run_delay += delta;

	rq_sched_info_dequeued(rq, delta);
}

/*
 * Called when a task finally hits the CPU.  We can now calculate how
 * long it was waiting to run.  We also note when it began so that we
 * can keep stats on how long its timeslice is.
 */
static void sched_info_arrive(struct rq *rq, struct task_struct *t)
{
	unsigned long long now = rq_clock(rq), delta = 0;

    /*如果被切换进来前在运行进程中排队*/
	if (t->sched_info.last_queued)
        /*计算排队等待的时间长度*/  
		delta = now - t->sched_info.last_queued;
    /*因为进程将被切换进来运行，设定last_queued为0*/  
	sched_info_reset_dequeued(t);
    /*更新进程在运行队列里面等待的时间*/ 
	t->sched_info.run_delay += delta;
    /*更新最后一次运行的时间*/
	t->sched_info.last_arrival = now;
    /*cpu上运行的次数加一*/ 
	t->sched_info.pcount++;
    /*更新rq中rq_sched_info中的对应的变量*/
	rq_sched_info_arrive(rq, delta);
}

/*
 * This function is only called from enqueue_task(), but also only updates
 * the timestamp if it is already not set.  It's assumed that
 * sched_info_dequeued() will clear that stamp when appropriate.
 */
static inline void sched_info_queued(struct rq *rq, struct task_struct *t)
{
	if (unlikely(sched_info_on())) {
		if (!t->sched_info.last_queued)
			t->sched_info.last_queued = rq_clock(rq);
	}
}

/*
 * Called when a process ceases being the active-running process involuntarily
 * due, typically, to expiring its time slice (this may also be called when
 * switching to the idle task).  Now we can calculate how long we ran.
 * Also, if the process is still in the TASK_RUNNING state, call
 * sched_info_queued() to mark that it has now again started waiting on
 * the runqueue.
 */
static inline void sched_info_depart(struct rq *rq, struct task_struct *t)
{
    /*计算在进程在rq中运行的时间长度*/  
	unsigned long long delta = rq_clock(rq) - t->sched_info.last_arrival;

    /*更新RunQueue中的Task所得到CPU執行時間的累加值.*/
	rq_sched_info_depart(rq, delta);

    /*如果被切换出去进程的状态是运行状态 
        那么将进程sched_info.last_queued设置为rq的clock 
        last_queued为最后一次排队等待运行的时间*/ 
	if (t->state == TASK_RUNNING)
		sched_info_queued(rq, t);
}

/*
 * Called when tasks are switched involuntarily due, typically, to expiring
 * their time slice.  (This may also be called when switching to or from
 * the idle task.)  We are only called when prev != next.
 */
static inline void
__sched_info_switch(struct rq *rq, struct task_struct *prev, struct task_struct *next)
{
	/*
	 * prev now departs the CPU.  It's not interesting to record
	 * stats about how efficient we were at scheduling the idle
	 * process, however.
	 */
    /*如果被切换出去的进程不是idle进程*/
	if (prev != rq->idle)
        /*更新prev进程和他对应rq的相关变量*/
		sched_info_depart(rq, prev);

    /*如果切换进来的进程不是idle进程*/ 
	if (next != rq->idle)
        /*更新next进程和对应队列的相关变量*/
		sched_info_arrive(rq, next);
}

static inline void
sched_info_switch(struct rq *rq, struct task_struct *prev, struct task_struct *next)
{
	if (unlikely(sched_info_on()))
		__sched_info_switch(rq, prev, next);
}

#else /* !CONFIG_SCHED_INFO: */
# define sched_info_queued(rq, t)	do { } while (0)
# define sched_info_reset_dequeued(t)	do { } while (0)
# define sched_info_dequeued(rq, t)	do { } while (0)
# define sched_info_depart(rq, t)	do { } while (0)
# define sched_info_arrive(rq, next)	do { } while (0)
# define sched_info_switch(rq, t, next)	do { } while (0)
#endif /* CONFIG_SCHED_INFO */
