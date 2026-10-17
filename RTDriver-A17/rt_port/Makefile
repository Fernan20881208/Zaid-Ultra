# SPDX-License-Identifier: GPL-2.0-only
ifneq ($(KERNELRELEASE),)
obj-m += rt_a17_diag.o
else
ifeq ($(strip $(KERNEL_SRC)),)
$(error KERNEL_SRC is required to build this Samsung kernel module)
endif
.PHONY: all modules modules_install clean
all: modules
modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) O=$(O) modules
modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) O=$(O) modules_install
clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) O=$(O) clean
endif
