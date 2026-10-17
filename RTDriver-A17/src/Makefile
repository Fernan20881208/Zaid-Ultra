# SPDX-License-Identifier: GPL-2.0-only
# Samsung Kleaf kernel_module invokes `make -C <module dir>` with no goal.
# Use a real default target to invoke the 6.12 kernel build system.
# Inside Kbuild, KERNELRELEASE is set and only obj-m must be evaluated.
ifneq ($(KERNELRELEASE),)
obj-m += a17_compat_probe.o
else
ifeq ($(strip $(KERNEL_SRC)),)
$(error KERNEL_SRC must point to the Samsung 6.12 kernel source tree)
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
