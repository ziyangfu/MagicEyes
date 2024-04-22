
/*!
*	\brief 用于跟踪用户空间进程的内存使用情况。具体功能是在用户空间进程切换时，
*			记录切换前进程的内存信息，并将这些信息写入环形缓冲区中
*/
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "mem_watcher.h"

char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 256 * 1024);
	__type(key, pid_t);
	__type(value, int);
} last_val SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 256 * 1024);
} rb SEC(".maps");

pid_t user_pid = 0;


SEC("kprobe/finish_task_switch")
int BPF_KPROBE(finish_task_switch, struct task_struct *prev) {
	struct procstat_event *e;
	//struct percpu_counter rss = {};
	struct mm_struct *mms;
	long long *t;
	pid_t pid = bpf_get_current_pid_tgid() >> 32;
	if (pid == user_pid)
		return 0;

	pid_t p_pid = BPF_CORE_READ(prev, pid);
	if (p_pid == user_pid)
		return 0;
	
	int val = 1;
	int *last_pid;
	last_pid = bpf_map_lookup_elem(&last_val, &p_pid);
	if (!last_pid) {
		bpf_map_update_elem(&last_val, &p_pid, &val, BPF_ANY);
	}
	else if(*last_pid == val) {
		return 0;
	}

	e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
	if (!e)
		return 0;

	e->pid = BPF_CORE_READ(prev, pid);
	e->vsize = BPF_CORE_READ(prev, mm, total_vm);
	e->Vdata = BPF_CORE_READ(prev, mm, data_vm);
	e->Vstk = BPF_CORE_READ(prev, mm, stack_vm);
	e->nvcsw = BPF_CORE_READ(prev, nvcsw);
	e->nivcsw = BPF_CORE_READ(prev, nivcsw);

	//rss = *BPF_CORE_READ(prev, mm, rss_stat);
	//t = (long long *)(rss.count);

	t = (long long *)(BPF_CORE_READ(prev, mm, rss_stat).count);
	e->rssfile = *t;  		// MM_FILEPAGES
	e->rssanon = *(t + 1);  // MM_ANONPAGES
	e->vswap = *(t + 2);	// MM_SWAPENTS
	e->rssshmem = *(t + 3);	// MM_SHMEMPAGES
	e->size = *t + *(t + 1) + *(t + 3);

	bpf_ringbuf_submit(e, 0);
	return 0;
}