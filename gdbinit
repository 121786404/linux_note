file vmlinux
set backtrace limit 15
#b smp_setup_processor_id
#b start_kernel
#b alloc_thread_stack_node
#b current_thread_info
#b do_DataAbort
#b do_page_fault
#b do_anonymous_page
#b do_fault
#b do_swap_page
#b do_wp_page
#b find_pid_ns
#b find_vpid
#b find_get_pid
b _do_fork
#b sys_fork
#b sys_vfork
#b smp_init
#b arm_dt_init_cpu_maps
#b smp_prepare_cpus
#b sched_init_domains

#init_task