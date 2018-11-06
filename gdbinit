file vmlinux
set backtrace limit 15
b smp_setup_processor_id
#b do_DataAbort
#b do_page_fault
#b do_anonymous_page
#b do_fault
#b do_swap_page
#b do_wp_page