KBUILD_CFLAGS   += -O0
KBUILD_LDFLAGS  += --warn-unresolved-symbols

尝试使用上面的 flag 编译内核，目前看不到控制台，但是gdb显示 已经起来了