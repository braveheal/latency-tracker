/*
 * rt.c
 *
 * Copyright (C) 2015 Julien Desfossez <jdesfossez@efficios.com>
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; only version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/file.h>
#include <linux/dcache.h>
#include <linux/jhash.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/stacktrace.h>
#include <linux/kprobes.h>
#include <asm/stacktrace.h>
#include "../latency_tracker.h"
#include "../wrapper/tracepoint.h"
#include "../wrapper/trace-clock.h"

#include <trace/events/latency_tracker.h>

/*
 * Threshold to execute the callback (microseconds).
 */
#define DEFAULT_USEC_RT_THRESH 5 * 1000 * 1000
/*
 * Timeout to execute the callback (microseconds).
 */
#define DEFAULT_USEC_RT_TIMEOUT 0

/*
 * microseconds because we can't guarantee the passing of 64-bit
 * arguments to insmod on all architectures.
 */
static unsigned long usec_threshold = DEFAULT_USEC_RT_THRESH;
module_param(usec_threshold, ulong, 0644);
MODULE_PARM_DESC(usec_threshold, "Threshold in microseconds");

static unsigned long usec_timeout = DEFAULT_USEC_RT_TIMEOUT;
module_param(usec_timeout, ulong, 0644);
MODULE_PARM_DESC(usec_timeout, "Timeout in microseconds");

static struct latency_tracker *tracker;

static int cnt = 0;
static int failed_event_in = 0;

enum rt_key_type {
	KEY_DO_IRQ = 0,
	KEY_HARDIRQ = 1,
	KEY_RAISE_SOFTIRQ = 2,
	KEY_SOFTIRQ = 3,
	KEY_WAKEUP = 4,
};

enum event_out_types {
	OUT_IRQHANDLER_NO_CB = 0,
};

struct do_irq_key_t {
	unsigned int cpu;
	enum rt_key_type type;
} __attribute__((__packed__));

struct hardirq_key_t {
	unsigned int cpu;
	enum rt_key_type type;
} __attribute__((__packed__));

struct raise_softirq_key_t {
	unsigned int cpu;
	unsigned int vector;
	enum rt_key_type type;
} __attribute__((__packed__));

struct softirq_key_t {
	unsigned int cpu;
	enum rt_key_type type;
} __attribute__((__packed__));

struct wakeup_key_t {
	int pid;
	enum rt_key_type type;
} __attribute__((__packed__));

#if 0
static int print_trace_stack(void *data, char *name)
{
        return 0;
}

static void
__save_stack_address(void *data, unsigned long addr, bool reliable, bool nosched)
{
        struct stack_trace *trace = data;
#ifdef CONFIG_FRAME_POINTER
        if (!reliable)
                return;
#endif
        if (nosched && in_sched_functions(addr))
                return;
        if (trace->skip > 0) {
                trace->skip--;
                return;
        }
        if (trace->nr_entries < trace->max_entries)
                trace->entries[trace->nr_entries++] = addr;
}

static void save_stack_address(void *data, unsigned long addr, int reliable)
{
        return __save_stack_address(data, addr, reliable, false);
}

static const struct stacktrace_ops backtrace_ops = {
        .stack                  = print_trace_stack,
        .address                = save_stack_address,
        .walk_stack             = print_context_stack,
};

static
void extract_stack(struct task_struct *p, char *stacktxt, uint64_t delay, int skip)
{
	struct stack_trace trace;
	unsigned long entries[32];
	char tmp[48];
	int i, j;
	size_t frame_len;

	trace.nr_entries = 0;
	trace.max_entries = ARRAY_SIZE(entries);
	trace.entries = entries;
	trace.skip = 0;
	dump_trace(p, NULL, NULL, 0, &backtrace_ops, &trace);
	//	print_stack_trace(&trace, 0);

	j = 0;
	for (i = 0; i < trace.nr_entries; i++) {
		if (i < skip)
			continue;
		snprintf(tmp, 48, "%pS\n", (void *) trace.entries[i]);
		frame_len = strlen(tmp);
		snprintf(stacktxt + j, MAX_STACK_TXT - j, tmp);
		j += frame_len;
		if (MAX_STACK_TXT - j < 0)
			return;
	}
	//printk("%s\n%llu\n\n", p->comm, delay/1000);
}
#endif

static
void rt_cb(struct latency_tracker_event_ctx *ctx)
{
	unsigned int cb_out_id = latency_tracker_event_ctx_get_cb_out_id(ctx);

	if (cb_out_id == OUT_IRQHANDLER_NO_CB)
		return;
#if 0
	uint64_t end_ts = latency_tracker_event_ctx_get_end_ts(ctx);
	uint64_t start_ts = latency_tracker_event_ctx_get_start_ts(ctx);
	enum latency_tracker_cb_flag cb_flag = latency_tracker_event_ctx_get_cb_flag(ctx);
	struct schedkey *key = (struct schedkey *) latency_tracker_event_ctx_get_key(ctx)->key;
	struct rt_tracker *rt_priv =
		(struct rt_tracker *) latency_tracker_get_priv(tracker);
	struct task_struct *p;
	char stacktxt[MAX_STACK_TXT];
	u64 delay;

	if (cb_flag != LATENCY_TRACKER_CB_NORMAL)
		return;
	if (cb_out_id == SCHED_EXIT_DIED)
		return;

	delay = (end_ts - start_ts) / 1000;

	rcu_read_lock();
	p = pid_task(find_vpid(key->pid), PIDTYPE_PID);
	if (!p)
		goto end;
//	printk("rt: sched_switch %s (%d) %llu us\n", p->comm, key->pid, delay);
	extract_stack(p, stacktxt, delay, 0);
	trace_latency_tracker_rt_sched_switch(p->comm, key->pid, end_ts - start_ts,
			cb_flag, stacktxt);
	cnt++;
	rt_handle_proc(rt_priv, end_ts);

end:
	rcu_read_unlock();
#endif
}

static
void probe_sched_switch(void *ignore, struct task_struct *prev,
		struct task_struct *next)
{
#if 0
	struct schedkey key;
	enum latency_tracker_event_in_ret ret;

	rcu_read_lock();
	if (!next || !prev)
		goto end;
	current_pid[prev->on_cpu] = next->pid;

	key.pid = prev->pid;
	key.cpu = smp_processor_id();
	ret = latency_tracker_event_in(tracker, &key, sizeof(key),
			1, latency_tracker_get_priv(tracker));

	key.pid = next->pid;
	key.cpu = smp_processor_id();
	latency_tracker_event_out(tracker, &key, sizeof(key),
			SCHED_EXIT_NORMAL);
end:
	rcu_read_unlock();
#endif
}

static
int entry_do_irq(struct kretprobe_instance *p, struct pt_regs *regs)
{
	enum latency_tracker_event_in_ret ret;
	struct do_irq_key_t key;

	key.cpu = smp_processor_id();
	key.type = KEY_DO_IRQ;
	ret = _latency_tracker_event_in(tracker, &key, sizeof(key), 1, 0, NULL);
	if (ret != LATENCY_TRACKER_OK)
		failed_event_in++;

	return 0;
}

static
struct kretprobe probe_do_irq = {
	.entry_handler = entry_do_irq,
	.handler = NULL,
	.kp.symbol_name = "do_IRQ",
};

static
void probe_irq_handler_entry(void *ignore, int irq, struct irqaction *action)
{
	struct do_irq_key_t do_irq_key;
	struct hardirq_key_t hardirq_key;
	struct latency_tracker_event *s;
	u64 orig_ts;
	int ret;

	do_irq_key.cpu = smp_processor_id();
	do_irq_key.type = KEY_DO_IRQ;

	/* Extract the do_irq event to get the timestamp of the beginning */
	s = latency_tracker_get_event(tracker, &do_irq_key, sizeof(do_irq_key));
	if (!s)
		return;
	orig_ts = latency_tracker_event_get_start_ts(s);
	latency_tracker_put_event(s);
	latency_tracker_event_out(tracker, &do_irq_key, sizeof(do_irq_key),
			OUT_IRQHANDLER_NO_CB);

	/*
	 * Replace the event with the new information but keep the original
	 * timestamp.
	 */
	hardirq_key.cpu = smp_processor_id();
	hardirq_key.type = KEY_HARDIRQ;
	ret = _latency_tracker_event_in(tracker, &hardirq_key,
			sizeof(hardirq_key), 1, orig_ts, NULL);
	if (ret != LATENCY_TRACKER_OK)
		failed_event_in++;
}

static
void probe_irq_handler_exit(void *ignore, int irq, struct irqaction *action,
		int ret)
{
	struct hardirq_key_t hardirq_key;

	/*
	 * If there is an IRQ event corresponding to this CPU in the HT,
	 * it means that the IRQ was not related to a RT user-space process.
	 * Otherwise it would have been removed from the softirq handler.
	 */
	hardirq_key.cpu = smp_processor_id();
	latency_tracker_event_out(tracker, &hardirq_key, sizeof(hardirq_key),
			OUT_IRQHANDLER_NO_CB);
}

static
void probe_softirq_raise(void *ignore, unsigned int vec_nr)
{
	struct hardirq_key_t hardirq_key;
	struct raise_softirq_key_t raise_softirq_key;
	struct latency_tracker_event *s;
	u64 orig_ts;
	int ret;

	hardirq_key.cpu = smp_processor_id();
	hardirq_key.type = KEY_HARDIRQ;

	/* Extract the hardirq event to get the timestamp of the beginning */
	s = latency_tracker_get_event(tracker, &hardirq_key, sizeof(hardirq_key));
	if (!s)
		return;
	orig_ts = latency_tracker_event_get_start_ts(s);
	latency_tracker_put_event(s);
	latency_tracker_event_out(tracker, &hardirq_key, sizeof(hardirq_key),
			OUT_IRQHANDLER_NO_CB);

	/*
	 * Replace the event with the new information but keep the original
	 * timestamp.
	 */
	raise_softirq_key.cpu = smp_processor_id();
	raise_softirq_key.vector = vec_nr;
	raise_softirq_key.type = KEY_RAISE_SOFTIRQ;
	/*
	 * Check if this softirq was already raised on this CPU, if it is we
	 * don't have a way to distinguish the 2 raises, so we stop following
	 * the event here.
	 */
	s = latency_tracker_get_event(tracker, &raise_softirq_key, sizeof(raise_softirq_key));
	if (s) {
		latency_tracker_put_event(s);
		return;
	}

	ret = _latency_tracker_event_in(tracker, &raise_softirq_key,
			sizeof(raise_softirq_key), 1, orig_ts, NULL);
	if (ret != LATENCY_TRACKER_OK)
		failed_event_in++;
}

static
void probe_softirq_entry(void *ignore, unsigned int vec_nr)
{
	struct raise_softirq_key_t raise_softirq_key;
	struct softirq_key_t softirq_key;
	struct latency_tracker_event *s;
	u64 orig_ts;
	int ret;

	raise_softirq_key.cpu = smp_processor_id();
	raise_softirq_key.vector = vec_nr;
	raise_softirq_key.type = KEY_RAISE_SOFTIRQ;

	/*
	 * If we did not see a raise, we cannot link this event to a hardirq.
	 */
	s = latency_tracker_get_event(tracker, &raise_softirq_key,
			sizeof(raise_softirq_key));
	if (!s)
		return;
	/*
	 * Get the original timestamp and remove the raise event.
	 */
	orig_ts = latency_tracker_event_get_start_ts(s);
	latency_tracker_put_event(s);
	latency_tracker_event_out(tracker, &raise_softirq_key,
			sizeof(raise_softirq_key), OUT_IRQHANDLER_NO_CB);

	/*
	 * Insert the softirq_entry event.
	 * Use the CPU as key on non-RT kernel and PID on PREEMPT_RT.
	 */
	softirq_key.cpu = smp_processor_id();
	softirq_key.type = KEY_SOFTIRQ;
	ret = _latency_tracker_event_in(tracker, &softirq_key,
			sizeof(softirq_key), 1, orig_ts, NULL);
	if (ret != LATENCY_TRACKER_OK)
		failed_event_in++;
}

static
void probe_softirq_exit(void *ignore, unsigned int vec_nr)
{
	struct softirq_key_t softirq_key;

	/*
	 * Just cleanup the softirq_entry event
	 */
	softirq_key.cpu = smp_processor_id();
	softirq_key.type = KEY_SOFTIRQ;
	latency_tracker_event_out(tracker, &softirq_key, sizeof(softirq_key),
			OUT_IRQHANDLER_NO_CB);
}

static
void probe_sched_wakeup(void *ignore, struct task_struct *p, int success)
{
	/*
	 * On a non-RT kernel, if we are here while handling a softirq, lookup
	 * the softirq currently active on the current CPU to get the origin
	 * timestamp. If we are here in a thread context, we cannot deduce
	 * anything (need sched_waking instead).
	 * On a PREEMPT_RT we match on the PID of the current task instead
	 * of the CPU.
	 */
	struct softirq_key_t softirq_key;
	struct wakeup_key_t wakeup_key;
	struct latency_tracker_event *s;
	u64 orig_ts;
	int ret;

	/* FIXME: is it the right RCU magic to make sure p stays alive ? */
	rcu_read_lock_sched_notrace();
	if (!p)
		goto end;

	/* In order of nesting */
	/* FIXME: can we detect mce/smi ? do we care ? */
	if (in_nmi()) {
		/* TODO */
		goto end;
	} else if (in_irq()) {
		/* TODO */
		goto end;
	} else if (in_serving_softirq()) {
		/*
		 * Just cleanup the softirq_entry event
		 */
		softirq_key.cpu = smp_processor_id();
		softirq_key.type = KEY_SOFTIRQ;

		s = latency_tracker_get_event(tracker, &softirq_key,
				sizeof(softirq_key));
		if (!s)
			goto end;
		orig_ts = latency_tracker_event_get_start_ts(s);
		latency_tracker_put_event(s);
	} else {
		goto end;
	}

	wakeup_key.pid = p->pid;
	wakeup_key.type = KEY_WAKEUP;
	ret = _latency_tracker_event_in(tracker, &wakeup_key,
			sizeof(wakeup_key), 1, orig_ts, NULL);
	if (ret != LATENCY_TRACKER_OK)
		failed_event_in++;

end:
	rcu_read_unlock_sched_notrace();
}

static
int __init rt_init(void)
{
	int ret;

	tracker = latency_tracker_create();
	if (!tracker)
		goto error;
	latency_tracker_set_startup_events(tracker, 2000);
	latency_tracker_set_max_resize(tracker, 10000);
	latency_tracker_set_timer_period(tracker, 100000000);
	latency_tracker_set_threshold(tracker, usec_threshold * 1000);
	latency_tracker_set_timeout(tracker, usec_timeout * 1000);
	latency_tracker_set_callback(tracker, rt_cb);
	ret = latency_tracker_enable(tracker);
	if (ret)
		goto error;

	ret = lttng_wrapper_tracepoint_probe_register("irq_handler_entry",
			probe_irq_handler_entry, NULL);
	WARN_ON(ret);

	ret = lttng_wrapper_tracepoint_probe_register("irq_handler_exit",
			probe_irq_handler_exit, NULL);
	WARN_ON(ret);

	ret = lttng_wrapper_tracepoint_probe_register("softirq_raise",
			probe_softirq_raise, NULL);
	WARN_ON(ret);

	ret = lttng_wrapper_tracepoint_probe_register("softirq_entry",
			probe_softirq_entry, NULL);
	WARN_ON(ret);

	ret = lttng_wrapper_tracepoint_probe_register("softirq_exit",
			probe_softirq_exit, NULL);
	WARN_ON(ret);

	ret = lttng_wrapper_tracepoint_probe_register("sched_switch",
			probe_sched_switch, NULL);
	WARN_ON(ret);

	ret = lttng_wrapper_tracepoint_probe_register("sched_wakeup",
			probe_sched_wakeup, NULL);
	WARN_ON(ret);

	ret = register_kretprobe(&probe_do_irq);
	WARN_ON(ret);

	goto end;

error:
	ret = -1;
end:
	return ret;
}
module_init(rt_init);

static
void __exit rt_exit(void)
{
	uint64_t skipped;
	//struct rt_tracker *rt_priv;

	lttng_wrapper_tracepoint_probe_unregister("sched_switch",
			probe_sched_switch, NULL);
	lttng_wrapper_tracepoint_probe_unregister("sched_wakeup",
			probe_sched_wakeup, NULL);
	lttng_wrapper_tracepoint_probe_unregister("irq_handler_entry",
			probe_irq_handler_entry, NULL);
	lttng_wrapper_tracepoint_probe_unregister("irq_handler_exit",
			probe_irq_handler_exit, NULL);
	lttng_wrapper_tracepoint_probe_unregister("softirq_raise",
			probe_softirq_raise, NULL);
	lttng_wrapper_tracepoint_probe_unregister("softirq_entry",
			probe_softirq_entry, NULL);
	lttng_wrapper_tracepoint_probe_unregister("softirq_exit",
			probe_softirq_exit, NULL);
	unregister_kretprobe(&probe_do_irq);
	tracepoint_synchronize_unregister();
	skipped = latency_tracker_skipped_count(tracker);
	latency_tracker_destroy(tracker);
	printk("Missed events : %llu\n", skipped);
	printk("Total rt alerts : %d\n", cnt);
}
module_exit(rt_exit);

MODULE_AUTHOR("Julien Desfossez <jdesfossez@efficios.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
