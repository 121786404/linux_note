file vmlinux
set backtrace limit 15
#b do_DataAbort
#b do_page_fault
#b do_anonymous_page
#b do_fault
#b do_swap_page
#b do_wp_page