obj-y += board.o
obj-$(CONFIG_RISCV_MMODE) += reset.o
ifndef CONFIG_XPL_BUILD
obj-$(CONFIG_BOARD_LATE_INIT) += board_late.o
endif
obj-$(CONFIG_XPL_BUILD) += spl.o
obj-$(CONFIG_XPL_BUILD) += dram_init.o dram_lib.o

# The DRAM routine imported from the Allwinner BSP uses Andes custom opcodes.
# It runs on the A27 after the E907 trampoline has released that CPU.
AFLAGS_dram_lib.o := -march=rv32imafdc -mabi=ilp32d -mcmodel=medany
