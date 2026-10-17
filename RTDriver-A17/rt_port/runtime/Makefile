# SPDX-License-Identifier: GPL-2.0-only
ifneq ($(KERNELRELEASE),)
obj-m += rt_a17.o
rt_a17-y := rt_main.o rt_breakpoints.o
else
ifeq ($(strip $(KERNEL_SRC)),)
$(error KERNEL_SRC is required to build this Samsung kernel module)
endif
.PHONY: all modules modules_install clean
all: modules
modules modules_install clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) O=$(O) $@
endif
